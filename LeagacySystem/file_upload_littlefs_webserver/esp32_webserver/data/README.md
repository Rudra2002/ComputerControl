# ESP32 Login Page

This project provides a beautiful animated login page that can be hosted on an ESP32 microcontroller.

## Features

- Responsive design with smooth animations
- Lightweight implementation suitable for ESP32
- Form validation
- Clean, modern UI

## Setup Instructions

1. Install the Arduino IDE and ESP32 board support
2. Install the following libraries:
   - ESPAsyncWebServer
   - AsyncTCP
   - SPIFFS
3. Update the WiFi credentials in the `esp32_webserver.ino` file
4. Upload the HTML, CSS, and JS files to SPIFFS
5. Upload the sketch to your ESP32
6. Access the login page by navigating to your ESP32's IP address in a web browser

## Default Credentials

- Username: admin
- Password: password123

## File Structure

- `index.html` - The main HTML file for the login page
- `style.css` - CSS styles and animations
- `script.js` - JavaScript for form handling and animations
- `esp32_webserver.ino` - Arduino sketch for the ESP32

## Customization

You can customize the colors, animations, and layout by modifying the CSS file.
