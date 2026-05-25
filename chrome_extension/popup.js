function formatUptime(ms) {
  const totalSec = Math.floor(ms / 1000);
  const h = Math.floor(totalSec / 3600);
  const m = Math.floor((totalSec % 3600) / 60);
  const s = totalSec % 60;
  if (h > 0) return `${h}h ${m}m ${s}s`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

function formatRelativeTime(timestamp) {
  const diff = Math.floor((Date.now() - timestamp) / 1000);
  if (diff < 60) return `${diff}s ago`;
  const mins = Math.floor(diff / 60);
  if (mins < 60) return `${mins}m ago`;
  const hours = Math.floor(mins / 60);
  return `${hours}h ago`;
}

function update(status) {
  const dot = document.getElementById("dot");
  const statusValue = document.getElementById("statusValue");
  const modeValue = document.getElementById("modeValue");
  const uptimeValue = document.getElementById("uptimeValue");
  const cmdCountValue = document.getElementById("cmdCountValue");
  const cmdList = document.getElementById("cmdList");
  const cmdEmpty = document.getElementById("cmdEmpty");
  const errorSection = document.getElementById("errorSection");
  const errorBody = document.getElementById("errorBody");

  if (status.connected) {
    dot.classList.add("on");
    statusValue.textContent = "Connected";
    statusValue.style.color = "#10b981";
  } else {
    dot.classList.remove("on");
    statusValue.textContent = "Offline";
    statusValue.style.color = "#ef4444";
  }

  modeValue.textContent = "Extension Bridge";

  if (status.startTime) {
    const uptime = Date.now() - status.startTime;
    uptimeValue.textContent = formatUptime(uptime);
  }

  cmdCountValue.textContent = status.commandCount || 0;

  // Render recent commands (last 5)
  const recent = (status.commandHistory || []).slice(0, 5);
  cmdList.innerHTML = "";

  if (recent.length === 0) {
    cmdEmpty.style.display = "block";
  } else {
    cmdEmpty.style.display = "none";
    recent.forEach((cmd) => {
      const item = document.createElement("div");
      item.className = "cmd-item";
      item.innerHTML = `<span class="cmd-type">${cmd.type}</span><span class="cmd-time">${formatRelativeTime(cmd.timestamp)}</span>`;
      cmdList.appendChild(item);
    });
  }

  // Error
  if (status.lastError) {
    errorSection.style.display = "block";
    errorBody.textContent = status.lastError;
  } else {
    errorSection.style.display = "none";
  }
}

// Error toggle
document.getElementById("errorToggle").addEventListener("click", () => {
  const toggle = document.getElementById("errorToggle");
  const body = document.getElementById("errorBody");
  toggle.classList.toggle("open");
  body.classList.toggle("open");
});

// Request status from background
chrome.runtime.sendMessage({ type: "get_status" }, (response) => {
  if (response) {
    update(response);
  }
});
