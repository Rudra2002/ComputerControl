document.addEventListener("DOMContentLoaded", () => {
  const loginForm = document.getElementById("loginForm");

  // Add animation class to form elements
  const formElements = document.querySelectorAll(".input-group, .remember-forgot, .login-btn");
  formElements.forEach((element) => {
    element.classList.add("animate");
  });

  if (loginForm) {
    loginForm.addEventListener("submit", (e) => {
      e.preventDefault();
      const username = document.getElementById("username").value;
      const password = document.getElementById("password").value;

      if (!username || !password) {
        alert("Please fill in all fields");
        return;
      }

      const loginBtn = document.querySelector(".login-btn");
      loginBtn.textContent = "Logging in...";
      loginBtn.disabled = true;

      fetch("/login", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
        },
        body: `username=${encodeURIComponent(username)}&password=${encodeURIComponent(password)}`,
      })
      .then((response) => {
        if (response.ok) {
          return response.text();
        }
        throw new Error("Login failed");
      })
      .then((data) => {
        loginBtn.textContent = "Success!";
        setTimeout(() => {
          window.location.href = "/dashboard";
        }, 1000);
      })
      .catch((error) => {
        loginBtn.textContent = "Login";
        loginBtn.disabled = false;
        alert("Login failed. Please check your credentials.");
      });
    });
  }
});
