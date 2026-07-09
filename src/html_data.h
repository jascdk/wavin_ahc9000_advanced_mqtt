#ifndef HTML_DATA_H
#define HTML_DATA_H

#include <Arduino.h>

const char* html_template = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Wavin Gateway</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root { --bg-color: #f0f2f5; --text-color: #333; --card-bg: white; --primary-color: #005eb8; --primary-hover: #004a94; --danger-color: #dc3545; --danger-hover: #c82333; --border-color: #eee; --label-color: #666; --shadow-color: rgba(0,0,0,0.08); }
    body.dark { --bg-color: #121212; --text-color: #e0e0e0; --card-bg: #1e1e1e; --primary-color: #bb86fc; --primary-hover: #a76ef4; --danger-color: #cf6679; --danger-hover: #b85668; --border-color: #333; --label-color: #aaa; --shadow-color: rgba(0,0,0,0.3); }
    
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: var(--bg-color); color: var(--text-color); margin: 0; padding: 20px; transition: background-color 0.3s, color 0.3s; }
    .container { max-width: 600px; margin: 0 auto; }
    .card { background: var(--card-bg); padding: 25px; border-radius: 8px; box-shadow: 0 2px 8px var(--shadow-color); margin-bottom: 20px; transition: all 0.3s ease; }
    h1 { color: #005eb8; margin-top: 0; text-align: center; font-size: 1.8em; margin-bottom: 20px; }
    body.dark h1 { color: var(--primary-color); }
    h2 { color: var(--primary-color); border-bottom: 2px solid var(--border-color); padding-bottom: 10px; margin-top: 0; margin-bottom: 15px; font-size: 1.2em; cursor: pointer; display: flex; justify-content: space-between; align-items: center; user-select: none; }
    h2::after { content: '▼'; font-size: 0.8em; color: #ccc; transition: transform 0.3s; }
    .card.collapsed h2::after { transform: rotate(-90deg); }
    .card.collapsed .card-content { display: none; }
    .card.collapsed h2 { border-bottom: none; margin-bottom: 0; }
    
    .info-row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid var(--border-color); }
    .info-row:last-child { border-bottom: none; }
    .info-label { font-weight: 500; color: var(--label-color); }
    .info-val { font-weight: 600; color: var(--text-color); }
    
    .btn { display: block; background: var(--primary-color); color: var(--card-bg); padding: 12px; text-decoration: none; border-radius: 6px; border: none; cursor: pointer; font-size: 16px; transition: background 0.2s; width: 100%; box-sizing: border-box; margin-bottom: 10px; text-align: center; font-weight: 500; }
    body.dark .btn { color: #121212; }
    .btn:hover { background: var(--primary-hover); }
    .btn-danger { background: var(--danger-color); }
    .btn-danger:hover { background: var(--danger-hover); }
    
    input[type="file"] { width: 100%; padding: 10px; border: 2px dashed #ccc; border-radius: 6px; box-sizing: border-box; background: #fafafa; margin-bottom: 15px; text-align: center; color: var(--text-color); }
    body.dark input[type="file"] { background: #2c2c2c; border-color: #444; }
    
    .footer { margin-top: 30px; font-size: 13px; color: #888; text-align: center; line-height: 1.5; border-top: 1px solid var(--border-color); padding-top: 20px; }
    .status-ok { color: #28a745; font-weight: bold; }
    .status-err { color: #dc3545; font-weight: bold; }
    .help { font-size: 0.9em; color: var(--label-color); margin-bottom: 15px; display: block; line-height: 1.4; }

    .switch { position: relative; display: inline-block; width: 50px; height: 24px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 24px; }
    .slider:before { position: absolute; content: ""; height: 16px; width: 16px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
    input:checked + .slider { background-color: var(--primary-color); }
    input:checked + .slider:before { transform: translateX(26px); }
  </style>
</head>
<body class="%DARK_MODE_CLASS%">
  <div class="container">
    <h1>Wavin AHC 9000 Gateway</h1>
    
    <div class="card">
      <h2 onclick="this.parentElement.classList.toggle('collapsed')">System Status</h2>
      <div class="card-content">
      <div class="info-row"><span class="info-label">Firmware</span><span class="info-val">%VERSION%</span></div>
      <div class="info-row"><span class="info-label">Uptime</span><span class="info-val">%UPTIME%</span></div>
      <div class="info-row"><span class="info-label">WiFi Signal</span><span class="info-val">%RSSI% dBm</span></div>
      <div class="info-row"><span class="info-label">IP Address</span><span class="info-val">%IP%</span></div>
      <div class="info-row"><span class="info-label">MQTT Status</span><span class="info-val">%MQTT_STATUS%</span></div>
      <div class="info-row"><span class="info-label">System Time</span><span class="info-val">%TIME%</span></div>
      </div>
    </div>

    <div class="card">
      <h2 onclick="this.parentElement.classList.toggle('collapsed')">Room Overview</h2>
      <div class="card-content">
        <div class="info-row" style="border-bottom: 2px solid var(--border-color); font-size: 0.85em; color: var(--label-color); text-transform: uppercase; letter-spacing: 0.5px;">
          <div style="flex:2;">Room</div><div style="flex:1; text-align:center;">Cur</div><div style="flex:1; text-align:center;">Set</div><div style="flex:1; text-align:right;">Bat/Sig</div>
        </div>
        %ROOM_LIST%
      </div>
    </div>

    <div class="card collapsed">
      <h2 onclick="this.parentElement.classList.toggle('collapsed')">Firmware Update</h2>
      <div class="card-content">
      <div class="help">Select a .bin file to update the device firmware.</div>
      <form id="upload_form" method="POST" action="/update" enctype="multipart/form-data">
        <input type="file" name="update" id="file_input" required>
        <input type="submit" class="btn" value="Update from File">
      </form>
      <div id="progress_container" style="display:none; margin-top: 15px;">
        <div style="background-color: #eee; border-radius: 4px; overflow: hidden;">
           <div id="progress_bar" style="width: 0%; height: 20px; background-color: #005eb8; transition: width 0.2s;"></div>
        </div>
        <div id="progress_text" style="text-align: center; font-size: 0.9em; color: #666; margin-top: 5px;">0%</div>
      </div>
      <div style="margin-top: 15px; border-top: 1px solid #eee; padding-top: 15px;">
        <div class="help">Or check GitHub for the latest release.</div>
        <form method="POST" action="/github_check">
           <button type="submit" class="btn" style="background-color: #24292e;">Check GitHub for Updates</button>
        </form>
      </div>
      </div>
    </div>

    <div class="card collapsed">
      <h2 onclick="this.parentElement.classList.toggle('collapsed')">UI Settings</h2>
      <div class="card-content">
        <div class="info-row">
          <span class="info-label">Dark Mode</span>
          <form id="dark_mode_form" method="POST" action="/toggle_dark_mode" style="margin:0;">
            <label class="switch">
              <input type="checkbox" id="dark_mode_toggle" onchange="this.form.submit()" %DARK_MODE_CHECKED%>
              <span class="slider"></span>
            </label>
          </form>
        </div>
      </div>
    </div>

    <div class="card collapsed">
      <h2 onclick="this.parentElement.classList.toggle('collapsed')">Debug</h2>
      <div class="card-content">
      <div class="help">Enable Telnet to view live system logs.</div>
      <div class="info-row"><span class="info-label">Telnet Status</span><span class="info-val">%TELNET_STATUS%</span></div>
      <form method="POST" action="/toggle_telnet" style="margin-top: 15px;">
        <button type="submit" class="btn">%TELNET_ACTION% Telnet</button>
      </form>
      <form method="POST" action="/discovery" style="margin-top: 10px;">
        <button type="submit" class="btn" style="background-color: #6c757d;">Resend HA Discovery</button>
      </form>
      </div>
    </div>

    <div class="card collapsed">
      <h2 onclick="this.parentElement.classList.toggle('collapsed')">System Control</h2>
      <div class="card-content">
      <div class="help">Backup settings, reboot, or reset the device.</div>
      <a href="/backup" class="btn" style="background-color: #5a6268;">Backup Configuration</a>
      
      <div style="margin-top: 15px; border-top: 1px solid var(--border-color); padding-top: 15px;">
        <div class="help">Restore configuration from backup file.</div>
        <form method="POST" action="/restore" enctype="multipart/form-data">
          <input type="file" name="restore_file" accept=".json" required>
          <button type="submit" class="btn" style="background-color: #6c757d;">Restore Configuration</button>
        </form>
      </div>

      <form method="POST" action="/reboot" onsubmit="return confirm('Are you sure you want to restart the device?');" style="margin-top: 15px; border-top: 1px solid var(--border-color); padding-top: 15px;">
        <button type="submit" class="btn">Reboot Device</button>
      </form>
      <form method="POST" action="/reset" onsubmit="return confirm('Are you sure? This will erase WiFi settings and reboot.');">
        <button type="submit" class="btn btn-danger">Factory Reset & AP Mode</button>
      </form>
      </div>
    </div>

    <div class="footer">
      Developed by <strong>Jacob J. Scherrebeck</strong><br>
      Wavin AHC 9000 MQTT Gateway
    </div>
  </div>
  <script>
    document.getElementById('upload_form').addEventListener('submit', function(e) {
      e.preventDefault();
      var fileInput = document.getElementById('file_input');
      var file = fileInput.files[0];
      if (!file) return;
      var formData = new FormData();
      formData.append('update', file);
      var xhr = new XMLHttpRequest();
      document.getElementById('progress_container').style.display = 'block';
      xhr.upload.addEventListener('progress', function(e) {
        if (e.lengthComputable) {
          var percent = Math.round((e.loaded / e.total) * 100);
          document.getElementById('progress_bar').style.width = percent + '%';
          document.getElementById('progress_text').innerText = percent + '% Uploaded';
        }
      });
      xhr.addEventListener('load', function() {
        if (xhr.status === 200) {
           document.getElementById('progress_bar').style.backgroundColor = '#28a745';
           document.getElementById('progress_text').innerText = xhr.responseText;
           setTimeout(function() { location.reload(); }, 15000); 
        } else {
           document.getElementById('progress_bar').style.backgroundColor = '#dc3545';
           document.getElementById('progress_text').innerText = 'Upload Failed';
        }
      });
      xhr.addEventListener('error', function() {
           document.getElementById('progress_bar').style.backgroundColor = '#dc3545';
           document.getElementById('progress_text').innerText = 'Upload Error';
      });
      xhr.open('POST', '/update');
      xhr.send(formData);
    });
  </script>
</body>
</html>
)rawliteral";

#endif