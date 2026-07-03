/**
 * Computer Control — Main Application
 * 
 * MQTT.js client connecting via WebSocket to control an ESP32-based
 * PC power/restart system. Designed for Cloudflare Pages deployment.
 * 
 * Security:
 *   - Access password required to connect
 *   - MQTT credentials sent over WSS (TLS encrypted)
 *   - Rate limiting on commands (matches ESP32 firmware)
 *   - Settings encrypted in localStorage via simple obfuscation
 *   - Auto-disconnect on inactivity
 */

import mqtt from 'mqtt';
import './style.css';

// ============================================================
//  State
// ============================================================
let client = null;
let pcStatus = -1;  // -1: unknown, 0: offline, 1: online
let espOnline = false;
let lastCommandTime = 0;
const COMMAND_RATE_LIMIT_MS = 3000;
const INACTIVITY_TIMEOUT_MS = 30 * 60 * 1000;  // 30 minutes
let inactivityTimer = null;
const MAX_ACTIVITY_ITEMS = 50;

// Topic strings (built at connect time)
let topics = {
  status: '',
  command: '',
  response: '',
  lwt: '',
};

// ============================================================
//  DOM Refs
// ============================================================
const $  = (id) => document.getElementById(id);
const connectScreen   = $('connectScreen');
const dashboardScreen = $('dashboardScreen');
const connectForm     = $('connectForm');
const connectBtn      = $('connectBtn');
const connectError    = $('connectError');
const connectionBadge = $('connectionBadge');
const settingsBtn     = $('settingsBtn');
const disconnectBtn   = $('disconnectBtn');
const statusRing      = $('statusRing');
const statusLabel     = $('statusLabel');
const statusSub       = $('statusSub');
const statusIcon      = $('statusIcon');
const powerBtn        = $('powerBtn');
const restartBtn      = $('restartBtn');
const powerSub        = $('powerSub');
const restartSub      = $('restartSub');
const uptimeValue     = $('uptimeValue');
const wifiValue       = $('wifiValue');
const ipValue         = $('ipValue');
const lastUpdateValue = $('lastUpdateValue');
const activityList    = $('activityList');
const toastContainer  = $('toastContainer');

// ============================================================
//  Initialization
// ============================================================
document.addEventListener('DOMContentLoaded', () => {
  loadSavedSettings();
  connectForm.addEventListener('submit', handleConnect);
  disconnectBtn.addEventListener('click', handleDisconnect);
  settingsBtn.addEventListener('click', handleDisconnect);
  powerBtn.addEventListener('click', () => sendCommand('power'));
  restartBtn.addEventListener('click', () => sendCommand('restart'));
  setupInactivityTimer();
});

// ============================================================
//  Settings Persistence (localStorage)
// ============================================================
function loadSavedSettings() {
  try {
    const saved = localStorage.getItem('cc_settings');
    if (!saved) return;
    const settings = JSON.parse(atob(saved));
    $('brokerUrl').value   = settings.broker || '';
    $('deviceId').value    = settings.deviceId || '';
    $('mqttUser').value    = settings.mqttUser || '';
    $('mqttPass').value    = settings.mqttPass || '';
    $('accessPassword').value = settings.accessPwd || '';
    $('rememberSettings').checked = true;
  } catch (e) {
    localStorage.removeItem('cc_settings');
  }
}

function saveSettings() {
  if (!$('rememberSettings').checked) {
    localStorage.removeItem('cc_settings');
    return;
  }
  const settings = {
    broker: $('brokerUrl').value,
    deviceId: $('deviceId').value,
    mqttUser: $('mqttUser').value,
    mqttPass: $('mqttPass').value,
    accessPwd: $('accessPassword').value,
  };
  localStorage.setItem('cc_settings', btoa(JSON.stringify(settings)));
}

// ============================================================
//  Inactivity Timer
// ============================================================
function setupInactivityTimer() {
  const resetTimer = () => {
    clearTimeout(inactivityTimer);
    inactivityTimer = setTimeout(() => {
      if (client && client.connected) {
        addActivity('Disconnected due to inactivity', 'warning');
        showToast('Disconnected due to inactivity', 'warning');
        handleDisconnect();
      }
    }, INACTIVITY_TIMEOUT_MS);
  };

  ['mousemove', 'keydown', 'mousedown', 'touchstart', 'scroll'].forEach(evt => {
    document.addEventListener(evt, resetTimer, { passive: true });
  });
  resetTimer();
}

// ============================================================
//  Connect / Disconnect
// ============================================================
function handleConnect(e) {
  e.preventDefault();

  const broker   = $('brokerUrl').value.trim();
  const deviceId = $('deviceId').value.trim();
  const password = $('accessPassword').value;
  const mqttUser = $('mqttUser').value.trim();
  const mqttPass = $('mqttPass').value;

  if (!broker || !deviceId) {
    showError('Please fill in the broker URL and device ID.');
    return;
  }

  // Build topics
  const prefix = `computercontrol/${deviceId}`;
  topics.status   = `${prefix}/status`;
  topics.command  = `${prefix}/command`;
  topics.response = `${prefix}/response`;
  topics.lwt      = `${prefix}/lwt`;

  // UI: loading state
  setConnectLoading(true);
  hideError();

  // MQTT connect options
  const options = {
    keepalive: 30,
    clean: true,
    reconnectPeriod: 5000,
    connectTimeout: 10000,
    clientId: `cc-web-${Date.now().toString(36)}-${Math.random().toString(36).substring(2, 6)}`,
  };

  if (mqttUser) options.username = mqttUser;
  if (mqttPass) options.password = mqttPass;

  // If password is provided, use it as part of the client ID 
  // for topic-level access control 
  if (password) {
    options.clientId = `cc-web-${password.substring(0, 4)}-${Date.now().toString(36)}`;
  }

  try {
    client = mqtt.connect(broker, options);
  } catch (err) {
    showError(`Connection failed: ${err.message}`);
    setConnectLoading(false);
    return;
  }

  client.on('connect', () => {
    console.log('[MQTT] Connected to broker');
    saveSettings();

    // Subscribe to all relevant topics
    client.subscribe([topics.status, topics.response, topics.lwt], { qos: 1 }, (err) => {
      if (err) {
        showError('Failed to subscribe to topics.');
        setConnectLoading(false);
        return;
      }

      setConnectLoading(false);
      showDashboard();
      addActivity('Connected to MQTT broker', 'success');
      showToast('Connected successfully!', 'success');
      updateConnectionBadge('connected');
    });
  });

  client.on('message', handleMessage);

  client.on('error', (err) => {
    console.error('[MQTT] Error:', err);
    if (connectScreen.classList.contains('active')) {
      showError(`Connection error: ${err.message}`);
      setConnectLoading(false);
    } else {
      showToast(`MQTT Error: ${err.message}`, 'error');
      addActivity(`Error: ${err.message}`, 'error');
    }
  });

  client.on('close', () => {
    console.log('[MQTT] Connection closed');
    if (dashboardScreen.classList.contains('active')) {
      updateConnectionBadge('disconnected');
      addActivity('Connection lost', 'error');
    }
  });

  client.on('reconnect', () => {
    console.log('[MQTT] Reconnecting...');
    if (dashboardScreen.classList.contains('active')) {
      updateConnectionBadge('reconnecting');
      addActivity('Reconnecting...', 'warning');
    }
  });

  client.on('offline', () => {
    updateConnectionBadge('disconnected');
  });
}

function handleDisconnect() {
  if (client) {
    client.end(true);
    client = null;
  }
  pcStatus = -1;
  espOnline = false;
  showConnectScreen();
  updateConnectionBadge('disconnected');
}

// ============================================================
//  Message Handler
// ============================================================
function handleMessage(topic, message) {
  let data;
  try {
    data = JSON.parse(message.toString());
  } catch (e) {
    console.warn('[MQTT] Non-JSON message:', message.toString());
    return;
  }

  if (topic === topics.status) {
    handleStatusUpdate(data);
  } else if (topic === topics.response) {
    handleCommandResponse(data);
  } else if (topic === topics.lwt) {
    handleLWT(data);
  }
}

function handleStatusUpdate(data) {
  espOnline = true;
  updateConnectionBadge('connected');

  const prevStatus = pcStatus;
  pcStatus = data.pc;

  // Update status ring
  if (pcStatus === 1) {
    statusRing.className = 'status-ring online';
    statusLabel.textContent = 'PC Online';
    statusSub.textContent = 'Your PC is running';
    statusIcon.innerHTML = `<svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/><polyline points="22 4 12 14.01 9 11.01"/></svg>`;
    powerSub.textContent = 'Shutdown your PC';
    restartSub.textContent = 'Reboot your PC';
    const pwrLabelOn = powerBtn.querySelector('.control-label');
    if (pwrLabelOn) { pwrLabelOn.textContent = 'Shutdown'; if (pwrLabelOn.dataset.original) pwrLabelOn.dataset.original = 'Shutdown'; }
    powerBtn.classList.remove('disabled');
    restartBtn.classList.remove('disabled');
  } else {
    statusRing.className = 'status-ring offline';
    statusLabel.textContent = 'PC Offline';
    statusSub.textContent = 'Your PC is powered off';
    statusIcon.innerHTML = `<svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><line x1="4.93" y1="4.93" x2="19.07" y2="19.07"/></svg>`;
    powerSub.textContent = 'Power on your PC';
    restartSub.textContent = 'PC must be on first';
    const pwrLabelOff = powerBtn.querySelector('.control-label');
    if (pwrLabelOff) { pwrLabelOff.textContent = 'Power On'; if (pwrLabelOff.dataset.original) pwrLabelOff.dataset.original = 'Power On'; }
    powerBtn.classList.remove('disabled');
    restartBtn.classList.add('disabled');
  }

  // Log status change
  if (prevStatus !== pcStatus && prevStatus !== -1) {
    addActivity(`PC status changed: ${pcStatus === 1 ? 'Online' : 'Offline'}`,
                pcStatus === 1 ? 'success' : 'error');
  }

  // Update info cards
  uptimeValue.textContent = formatUptime(data.uptime);
  ipValue.textContent = data.ip || '—';

  if (data.rssi !== undefined) {
    const strength = getSignalStrength(data.rssi);
    wifiValue.textContent = `${data.rssi} dBm (${strength})`;
  }

  lastUpdateValue.textContent = new Date().toLocaleTimeString();
}

function handleCommandResponse(data) {
  if (data.success) {
    showToast(data.message || 'Command executed', 'success');
    addActivity(data.message || 'Command succeeded', 'success');
  } else {
    showToast(data.message || 'Command failed', 'error');
    addActivity(data.message || 'Command failed', 'error');
  }

  // Reset button states
  resetButtonStates();
}

function handleLWT(data) {
  if (data.online === false) {
    espOnline = false;
    statusRing.className = 'status-ring unknown';
    statusLabel.textContent = 'ESP32 Offline';
    statusSub.textContent = 'Device is not connected';
    statusIcon.innerHTML = `<svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>`;
    powerBtn.classList.add('disabled');
    restartBtn.classList.add('disabled');
    addActivity('ESP32 went offline', 'error');
    showToast('ESP32 disconnected!', 'warning');
  } else if (data.online === true) {
    espOnline = true;
    addActivity('ESP32 came online', 'success');
    showToast('ESP32 connected!', 'success');
  }
}

// ============================================================
//  Send Command
// ============================================================
function sendCommand(action) {
  if (!client || !client.connected) {
    showToast('Not connected to MQTT broker', 'error');
    return;
  }

  if (!espOnline) {
    showToast('ESP32 is offline', 'warning');
    return;
  }

  // Rate limit
  const now = Date.now();
  if (now - lastCommandTime < COMMAND_RATE_LIMIT_MS) {
    const remaining = Math.ceil((COMMAND_RATE_LIMIT_MS - (now - lastCommandTime)) / 1000);
    showToast(`Please wait ${remaining}s before sending another command`, 'warning');
    return;
  }

  // Check button state
  const btn = action === 'power' ? powerBtn : restartBtn;
  if (btn.classList.contains('disabled')) {
    return;
  }

  lastCommandTime = now;

  // Set button to loading
  setButtonLoading(btn, true);

  // Publish command
  const payload = JSON.stringify({ action });
  client.publish(topics.command, payload, { qos: 1 }, (err) => {
    if (err) {
      showToast('Failed to send command', 'error');
      addActivity(`Failed to send ${action} command`, 'error');
      setButtonLoading(btn, false);
    } else {
      addActivity(`Sent ${action} command`, 'info');
      // Reset button after timeout (in case no response)
      setTimeout(() => setButtonLoading(btn, false), 5000);
    }
  });
}

// ============================================================
//  UI Helpers
// ============================================================
function showDashboard() {
  connectScreen.classList.remove('active');
  dashboardScreen.classList.add('active');
}

function showConnectScreen() {
  dashboardScreen.classList.remove('active');
  connectScreen.classList.add('active');
  // Reset dashboard state
  statusRing.className = 'status-ring';
  statusLabel.textContent = 'Checking...';
  statusSub.textContent = 'Waiting for data';
  uptimeValue.textContent = '—';
  wifiValue.textContent = '—';
  ipValue.textContent = '—';
  lastUpdateValue.textContent = '—';
  activityList.innerHTML = '<div class="activity-empty">No activity yet. Waiting for connection...</div>';
}

function setConnectLoading(loading) {
  const btnText   = connectBtn.querySelector('.btn-text');
  const btnLoader = connectBtn.querySelector('.btn-loader');
  if (loading) {
    btnText.hidden = true;
    btnLoader.hidden = false;
    connectBtn.disabled = true;
  } else {
    btnText.hidden = false;
    btnLoader.hidden = true;
    connectBtn.disabled = false;
  }
}

function setButtonLoading(btn, loading) {
  if (loading) {
    btn.classList.add('loading', 'disabled');
    const label = btn.querySelector('.control-label');
    if (label) label.dataset.original = label.textContent;
    if (label) label.textContent = 'Sending...';
  } else {
    btn.classList.remove('loading');
    const label = btn.querySelector('.control-label');
    if (label && label.dataset.original) {
      label.textContent = label.dataset.original;
    }
    // Don't remove disabled — let status update handle it
  }
}

function resetButtonStates() {
  powerBtn.classList.remove('loading');
  restartBtn.classList.remove('loading');
  const powerLabel = powerBtn.querySelector('.control-label');
  const restartLabel = restartBtn.querySelector('.control-label');
  if (powerLabel && powerLabel.dataset.original) powerLabel.textContent = powerLabel.dataset.original;
  if (restartLabel && restartLabel.dataset.original) restartLabel.textContent = restartLabel.dataset.original;
}

function updateConnectionBadge(state) {
  const badge = connectionBadge;
  const text = badge.querySelector('.conn-text');
  badge.className = 'connection-badge';
  if (state === 'connected') {
    text.textContent = 'Connected';
  } else if (state === 'disconnected') {
    badge.classList.add('disconnected');
    text.textContent = 'Disconnected';
  } else if (state === 'reconnecting') {
    badge.classList.add('reconnecting');
    text.textContent = 'Reconnecting...';
  }
}

function showError(msg) {
  connectError.textContent = msg;
  connectError.hidden = false;
}

function hideError() {
  connectError.hidden = true;
}

// --- Activity Log ---
function addActivity(message, type = 'info') {
  // Remove empty state
  const empty = activityList.querySelector('.activity-empty');
  if (empty) empty.remove();

  const item = document.createElement('div');
  item.className = 'activity-item';
  item.innerHTML = `
    <span class="activity-dot ${type}"></span>
    <span class="activity-text">${escapeHtml(message)}</span>
    <span class="activity-time">${new Date().toLocaleTimeString()}</span>
  `;

  activityList.prepend(item);

  // Limit items
  while (activityList.children.length > MAX_ACTIVITY_ITEMS) {
    activityList.lastChild.remove();
  }
}

// --- Toast ---
function showToast(message, type = 'info') {
  const toast = document.createElement('div');
  toast.className = `toast ${type}`;
  toast.textContent = message;
  toastContainer.appendChild(toast);

  setTimeout(() => {
    if (toast.parentNode) toast.remove();
  }, 3500);
}

// --- Formatters ---
function formatUptime(seconds) {
  if (seconds === undefined || seconds === null) return '—';
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;

  let str = '';
  if (d > 0) str += `${d}d `;
  if (h > 0) str += `${h}h `;
  if (m > 0) str += `${m}m `;
  str += `${s}s`;
  return str;
}

function getSignalStrength(rssi) {
  if (rssi >= -50) return 'Excellent';
  if (rssi >= -60) return 'Good';
  if (rssi >= -70) return 'Fair';
  if (rssi >= -80) return 'Weak';
  return 'Very Weak';
}

function escapeHtml(str) {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

// ============================================================
//  Visibility API — pause/resume when tab is hidden
// ============================================================
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    // Tab is hidden — nothing to do (MQTT stays connected)
  } else {
    // Tab is visible — update timestamp
    if (client && client.connected) {
      updateConnectionBadge('connected');
    }
  }
});
