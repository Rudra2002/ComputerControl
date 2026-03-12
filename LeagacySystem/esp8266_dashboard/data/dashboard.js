let isOnline = false
let updateInterval
let inactivityTimer

// Initialize the dashboard
document.addEventListener("DOMContentLoaded", () => {
  console.log("Dashboard initialized")
  updateStatus()
  startAutoUpdate()
  setupSessionTimeout()
})

function setupSessionTimeout() {
  // Reset timer on user activity
  function resetTimer() {
    clearTimeout(inactivityTimer)
    inactivityTimer = setTimeout(logout, 5 * 60 * 1000) // 5 minutes
  }

  document.addEventListener("mousemove", resetTimer)
  document.addEventListener("keydown", resetTimer)
  document.addEventListener("mousedown", resetTimer)
  document.addEventListener("touchstart", resetTimer)

  resetTimer()

  // Check session validity every 30s
  setInterval(() => {
    fetch("/ping")
      .then((response) => {
        if (!response.ok) {
          throw new Error("Session expired")
        }
      })
      .catch(() => {
        window.location.href = "/"
      })
  }, 30000)
}

function startAutoUpdate() {
  // Update status every 2 seconds
  updateInterval = setInterval(updateStatus, 2000)
}

function stopAutoUpdate() {
  if (updateInterval) {
    clearInterval(updateInterval)
  }
}

function updateStatus() {
  fetch("/status")
    .then((response) => {
      if (!response.ok) {
        if (response.status === 401) {
          window.location.href = "/"
          throw new Error("Unauthorized")
        }
        throw new Error(`HTTP error! status: ${response.status}`)
      }
      return response.json()
    })
    .then((data) => {
      updateUI(data)
      updateLastUpdateTime()
    })
    .catch((error) => {
      console.error("Error fetching status:", error)
      if (error.message !== "Unauthorized") {
        handleConnectionError()
      }
    })
}

function updateUI(data) {
  const statusDot = document.getElementById("statusDot")
  const statusText = document.getElementById("statusText")
  const powerBtn = document.getElementById("powerBtn")
  const restartBtn = document.getElementById("restartBtn")
  const wifiSignal = document.getElementById("wifiSignal")

  // Update PC status
  if (data.status === 1) {
    statusDot.className = "status-dot online"
    statusText.textContent = "PC Online"
    statusText.style.color = "#28a745"
    powerBtn.classList.add("disabled")
    restartBtn.classList.remove("disabled")
    isOnline = true
  } else {
    statusDot.className = "status-dot offline"
    statusText.textContent = "PC Offline"
    statusText.style.color = "#dc3545"
    powerBtn.classList.remove("disabled")
    restartBtn.classList.add("disabled")
    isOnline = false
  }

  // Update uptime
  const uptime = data.uptime
  document.getElementById("uptime").textContent = formatUptime(uptime)

  // Update WiFi signal strength
  if (data.rssi) {
    const signalStrength = getSignalStrength(data.rssi)
    wifiSignal.textContent = `${data.rssi} dBm (${signalStrength})`
  }

  // Update IP address if available
  if (data.ip) {
    document.getElementById("ipAddress").textContent = data.ip
  }
}

function formatUptime(seconds) {
  const days = Math.floor(seconds / 86400)
  const hours = Math.floor((seconds % 86400) / 3600)
  const minutes = Math.floor((seconds % 3600) / 60)
  const secs = seconds % 60

  let uptimeStr = ""
  if (days > 0) uptimeStr += `${days}d `
  if (hours > 0) uptimeStr += `${hours}h `
  if (minutes > 0) uptimeStr += `${minutes}m `
  uptimeStr += `${secs}s`

  return uptimeStr
}

function getSignalStrength(rssi) {
  if (rssi >= -50) return "Excellent"
  if (rssi >= -60) return "Good"
  if (rssi >= -70) return "Fair"
  if (rssi >= -80) return "Weak"
  return "Very Weak"
}

function updateLastUpdateTime() {
  const now = new Date()
  const timeString = now.toLocaleTimeString()
  document.getElementById("lastUpdate").textContent = timeString
}

function handleConnectionError() {
  const statusDot = document.getElementById("statusDot")
  const statusText = document.getElementById("statusText")

  statusText.textContent = "Connection Error"
  statusText.style.color = "#dc3545"
  statusDot.className = "status-dot offline"

  document.getElementById("lastUpdate").textContent = "Error"
}

function createRipple(event, button) {
  const ripple = button.querySelector(".btn-ripple")
  const rect = button.getBoundingClientRect()
  const size = Math.max(rect.width, rect.height)
  const x = event.clientX - rect.left - size / 2
  const y = event.clientY - rect.top - size / 2

  ripple.style.width = ripple.style.height = size + "px"
  ripple.style.left = x + "px"
  ripple.style.top = y + "px"
  ripple.style.animation = "none"
  ripple.offsetHeight // Trigger reflow
  ripple.style.animation = "ripple 0.6s linear"
}

function showButtonLoading(button, loadingText) {
  const originalContent = button.innerHTML
  button.classList.add("disabled", "loading")
  button.innerHTML = `<div class="btn-icon">⏳</div><span>${loadingText}</span><div class="btn-ripple"></div>`
  return originalContent
}

function hideButtonLoading(button, originalContent) {
  button.classList.remove("disabled", "loading")
  button.innerHTML = originalContent
}

function powerOn() {
  const button = document.getElementById("powerBtn")
  if (button.classList.contains("disabled")) return

  const originalContent = showButtonLoading(button, "Powering On...")

  fetch("/power", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
  })
    .then((response) => {
      if (!response.ok) {
        if (response.status === 401) {
          window.location.href = "/"
          throw new Error("Unauthorized")
        }
        throw new Error(`HTTP error! status: ${response.status}`)
      }
      return response.json()
    })
    .then((data) => {
      console.log("Power response:", data)
      setTimeout(() => {
        hideButtonLoading(button, originalContent)
        updateStatus() // Immediate status update
      }, 3000)
    })
    .catch((error) => {
      console.error("Power error:", error)
      if (error.message !== "Unauthorized") {
        hideButtonLoading(button, originalContent)
        alert("Failed to send power command. Please try again.")
      }
    })
}

function restart() {
  const button = document.getElementById("restartBtn")
  if (button.classList.contains("disabled")) return

  const originalContent = showButtonLoading(button, "Restarting...")

  fetch("/restart", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
  })
    .then((response) => {
      if (!response.ok) {
        if (response.status === 401) {
          window.location.href = "/"
          throw new Error("Unauthorized")
        }
        throw new Error(`HTTP error! status: ${response.status}`)
      }
      return response.json()
    })
    .then((data) => {
      console.log("Restart response:", data)
      setTimeout(() => {
        hideButtonLoading(button, originalContent)
        updateStatus() // Immediate status update
      }, 3000)
    })
    .catch((error) => {
      console.error("Restart error:", error)
      if (error.message !== "Unauthorized") {
        hideButtonLoading(button, originalContent)
        alert("Failed to send restart command. Please try again.")
      }
    })
}

function logout() {
  fetch("/logout").then(() => {
    window.location.href = "/"
  })
}

// Add ripple effect to buttons
document.addEventListener("click", (e) => {
  if (e.target.closest(".control-btn")) {
    const button = e.target.closest(".control-btn")
    if (!button.classList.contains("disabled")) {
      createRipple(e, button)
    }
  }
})

// Handle page visibility changes
document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    stopAutoUpdate()
  } else {
    updateStatus()
    startAutoUpdate()
  }
})

// Handle online/offline events
window.addEventListener("online", () => {
  console.log("Connection restored")
  updateStatus()
  startAutoUpdate()
})

window.addEventListener("offline", () => {
  console.log("Connection lost")
  handleConnectionError()
  stopAutoUpdate()
})
