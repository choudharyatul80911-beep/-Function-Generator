#include <SPI.h>
#include <Wire.h>
#include <WiFiS3.h>   // For Arduino UNO R4 WiFi
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- HARDWARE PIN DEFINITIONS ---
#define AD9833_FSYNC 10 
#define OLED_RESET -1   

// --- AD9833 COMMANDS ---
#define CMD_RESET 0x0100
#define CMD_NO_RESET 0x0000
#define CMD_PHASE_0 0xC000
#define CMD_SINE_OUT 0x2000
#define CMD_TRIANGLE_OUT 0x2002
#define CMD_SQUARE_OUT 0x2028

// --- DDS PARAMETERS ---
const double AD9833_MCLK = 25000000.0; 
double currentFrequency = 500.0;      
String currentWaveform = "Sine";       
uint16_t currentControlWord = CMD_SINE_OUT;

// --- OLED SETUP ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- WIFI SETUP ---
char ssid[] = "FuncGen_R4_Mod";    
char password[] = "12345678";  
int status = WL_IDLE_STATUS;
WiFiServer server(80); 

// --- PROTOTYPES ---
void AD9833_Write(uint16_t data);
void AD9833_SetFrequency(double freq, uint16_t waveType);
void drawDisplay();
void handleClient(WiFiClient client);

void setup() {
  Serial.begin(115200);
  pinMode(AD9833_FSYNC, OUTPUT);
  digitalWrite(AD9833_FSYNC, HIGH);
  SPI.begin();

  // OLED Init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed."));
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.display();
  }

  // WiFi Init
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module failed!");
    while (true);
  }
  
  status = WiFi.beginAP(ssid, password);
  if (status != WL_AP_LISTENING) {
    Serial.println("Creating AP failed");
    while (true);
  }
  
  server.begin();
  
  // AD9833 Initial Output
  AD9833_Write(CMD_RESET);
  delay(100);
  AD9833_SetFrequency(currentFrequency, currentControlWord); 
  AD9833_Write(CMD_NO_RESET); 

  drawDisplay();
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    handleClient(client);
  }
}

// --- AD9833 LOGIC ---
void AD9833_Write(uint16_t data) {
  digitalWrite(AD9833_FSYNC, LOW); 
  SPI.transfer16(data);
  digitalWrite(AD9833_FSYNC, HIGH);
}

void AD9833_SetFrequency(double freq, uint16_t waveType) {
  long freqWord = (long)((freq * pow(2, 28)) / AD9833_MCLK);
  uint16_t lowWord = freqWord & 0x3FFF;
  uint16_t highWord = (freqWord >> 14) & 0x3FFF;
  lowWord |= 0x4000;
  highWord |= 0x4000;
  
  AD9833_Write(0x2100); 
  AD9833_Write(lowWord);
  AD9833_Write(highWord);
  AD9833_Write(CMD_PHASE_0);
  AD9833_Write(waveType);
}

void drawDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("IP: "); display.println(WiFi.localIP());
  display.drawLine(0, 15, 128, 15, SSD1306_WHITE);
  display.setCursor(0, 25);
  display.print("Mode: "); display.println(currentWaveform);
  display.setCursor(0, 40);
  display.setTextSize(2);
  display.print((int)currentFrequency); display.println(" Hz");
  display.display();
}

// --- WEB SERVER HANDLER ---
void handleClient(WiFiClient client) {
  String currentLine = "";
  String requestLine = "";
  unsigned long startTime = millis();

  while (client.connected() && millis() - startTime < 1000) { 
    if (client.available()) {
      char c = client.read();
      if (requestLine.length() < 150) requestLine += c; 

      if (c == '\n' && currentLine.length() == 0) {
        
        // --- AJAX API FOR HARDWARE CONTROL ---
        if (requestLine.startsWith("GET /set?")) {
           // Parse Standard Params
           int startF = requestLine.indexOf("freq=") + 5;
           int endF = requestLine.indexOf("&", startF);
           String freqStr = requestLine.substring(startF, endF);
           
           int startW = requestLine.indexOf("wave=") + 5;
           int endW = requestLine.indexOf(" ", startW);
           String waveStr = requestLine.substring(startW, endW);
           
           // Note: Modulation is visual-heavy. 
           // For hardware simplicity, if Modulation is selected, we just output the Carrier Frequency (Sine).
           if (waveStr == "ASK" || waveStr == "FSK" || waveStr == "PSK") {
              waveStr = "Modulation"; 
              currentControlWord = CMD_SINE_OUT; 
           } else {
              if (waveStr == "Sine") currentControlWord = CMD_SINE_OUT;
              else if (waveStr == "Triangle") currentControlWord = CMD_TRIANGLE_OUT;
              else if (waveStr == "Square") currentControlWord = CMD_SQUARE_OUT;
           }

           double newFreq = freqStr.toDouble();
           if (newFreq > 0) {
             currentFrequency = newFreq;
             currentWaveform = waveStr;
             AD9833_SetFrequency(currentFrequency, currentControlWord);
             drawDisplay(); 
           }
           
           client.println("HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\nOK");
           break;
        }
        
        // --- SERVE WEB PAGE ---
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/html");
        client.println("Connection: close");
        client.println();

        client.println(F("<!DOCTYPE html><html lang='en'><head>"));
        client.println(F("<meta name='viewport' content='width=device-width, initial-scale=1.0'>"));
        client.println(F("<title>Signal Gen</title>"));
        
        // CSS
        client.println(F("<style>"));
        client.println(F("body { background-color: #0f172a; color: #e2e8f0; font-family: 'Segoe UI', sans-serif; display: flex; flex-direction: column; align-items: center; padding: 10px; margin: 0; }"));
        
        // Oscilloscope Box
        client.println(F(".scope-box { width: 100%; max-width: 500px; height: 220px; background: #000; border: 2px solid #334155; border-radius: 12px; position: relative; overflow: hidden; box-shadow: 0 0 20px rgba(0, 255, 127, 0.1); margin-bottom: 20px; }"));
        client.println(F(".grid { position: absolute; width: 100%; height: 100%; background-image: linear-gradient(#1e293b 1px, transparent 1px), linear-gradient(90deg, #1e293b 1px, transparent 1px); background-size: 20px 20px; opacity: 0.5; }"));
        client.println(F("canvas { width: 100%; height: 100%; position: relative; z-index: 2; }"));
        
        // Tab Switcher
        client.println(F(".tabs { display: flex; gap: 10px; margin-bottom: 15px; width: 100%; max-width: 500px; }"));
        client.println(F(".tab { flex: 1; padding: 10px; background: #1e293b; border: none; color: #94a3b8; border-radius: 8px; cursor: pointer; font-weight: bold; transition: 0.2s; }"));
        client.println(F(".tab.active { background: #10b981; color: #000; box-shadow: 0 0 10px #10b981; }"));

        // Controls
        client.println(F(".card { background: #1e293b; padding: 20px; border-radius: 12px; width: 100%; max-width: 500px; box-sizing: border-box; display: none; }"));
        client.println(F(".card.show { display: block; }"));
        
        client.println(F(".row { margin-bottom: 15px; }"));
        client.println(F("label { display: block; color: #94a3b8; margin-bottom: 5px; font-size: 0.9em; }"));
        client.println(F("input, select { width: 100%; padding: 12px; background: #334155; border: 1px solid #475569; border-radius: 6px; color: white; font-size: 1em; box-sizing: border-box; outline: none; }"));
        client.println(F("input:focus { border-color: #10b981; }"));
        
        client.println(F(".slider-val { float: right; color: #10b981; }"));
        client.println(F("</style></head><body>"));

        client.println(F("<h2 style='color:#10b981; margin-top:0;'>R4 Signal Gen</h2>"));
        
        // SCOPE
        client.println(F("<div class='scope-box'><div class='grid'></div><canvas id='cvs'></canvas></div>"));

        // TABS
        client.println(F("<div class='tabs'>"));
        client.println(F("<button class='tab active' onclick='setMode(0)'>Standard</button>"));
        client.println(F("<button class='tab' onclick='setMode(1)'>Modulation</button>"));
        client.println(F("</div>"));

        // --- CARD 1: STANDARD ---
        client.println(F("<div id='card-std' class='card show'>"));
        
        client.println(F("<div class='row'><label>Waveform</label>"));
        client.println(F("<select id='stdWave' onchange='upd()'><option value='Sine'>Sine</option><option value='Square'>Square</option><option value='Triangle'>Triangle</option></select></div>"));
        
        client.println(F("<div class='row'><label>Frequency <span id='lblFreq' class='slider-val'>500 Hz</span></label>"));
        client.println(F("<input type='range' id='stdFreq' min='1' max='1000' value='500' oninput='upd()'></div>"));
        
        client.println(F("</div>"));

        // --- CARD 2: MODULATION (ASK/FSK/PSK) ---
        client.println(F("<div id='card-mod' class='card'>"));
        
        client.println(F("<div class='row'><label>Modulation Scheme</label>"));
        client.println(F("<select id='modType' onchange='upd()'><option value='ASK'>ASK (Amplitude)</option><option value='FSK'>FSK (Frequency)</option><option value='PSK'>PSK (Phase)</option></select></div>"));
        
        client.println(F("<div class='row'><label>Binary Data (Enter 1s and 0s)</label>"));
        client.println(F("<input type='text' id='binData' value='10110' maxlength='16' oninput='upd()' placeholder='e.g. 10101'></div>"));
        
        client.println(F("</div>"));

        // JAVASCRIPT
        client.println(F("<script>"));
        client.println(F("const cvs = document.getElementById('cvs'); const ctx = cvs.getContext('2d');"));
        client.println(F("let mode = 0; let timer;")); // 0=Std, 1=Mod

        client.println(F("function setMode(m) {"));
        client.println(F("  mode = m;"));
        client.println(F("  document.querySelectorAll('.tab').forEach((t,i) => t.className = (i===m ? 'tab active' : 'tab'));"));
        client.println(F("  document.getElementById('card-std').className = (m===0 ? 'card show' : 'card');"));
        client.println(F("  document.getElementById('card-mod').className = (m===1 ? 'card show' : 'card');"));
        client.println(F("  upd();"));
        client.println(F("}"));

        client.println(F("function resize() { cvs.width = cvs.offsetWidth; cvs.height = cvs.offsetHeight; upd(); }"));
        client.println(F("window.addEventListener('resize', resize);"));

        client.println(F("function upd() {"));
        // Get UI Values
        client.println(F("  const stdW = document.getElementById('stdWave').value;"));
        client.println(F("  const stdF = document.getElementById('stdFreq').value;"));
        client.println(F("  document.getElementById('lblFreq').innerText = stdF + ' Hz';"));
        
        client.println(F("  const modT = document.getElementById('modType').value;"));
        client.println(F("  let bin = document.getElementById('binData').value.replace(/[^01]/g,'');"));
        client.println(F("  if(bin.length===0) bin='0';"));

        // Drawing Setup
        client.println(F("  ctx.clearRect(0,0,cvs.width,cvs.height);"));
        client.println(F("  ctx.lineWidth = 2; ctx.strokeStyle = '#10b981'; ctx.beginPath();"));
        
        client.println(F("  const w = cvs.width; const h = cvs.height; const mid = h/2;"));
        
        // --- PLOT LOGIC ---
        client.println(F("  if (mode === 0) {")); // STANDARD MODE
        client.println(F("    const cycles = stdF / 50;"));
        client.println(F("    for(let x=0; x<=w; x++) {"));
        client.println(F("      let t = (x/w) * Math.PI * 2 * cycles;"));
        client.println(F("      let y = mid;"));
        client.println(F("      if(stdW=='Sine') y += Math.sin(t) * (h/3);"));
        client.println(F("      else if(stdW=='Square') y += Math.sign(Math.sin(t)) * (h/3);"));
        client.println(F("      else if(stdW=='Triangle') y += (2 * Math.asin(Math.sin(t)) / Math.PI) * (h/3);"));
        client.println(F("      if(x==0) ctx.moveTo(x,y); else ctx.lineTo(x,y);"));
        client.println(F("    }"));
        client.println(F("  } else {")); // MODULATION MODE
        client.println(F("    const bits = bin.split('');"));
        client.println(F("    const pxPerBit = w / bits.length;"));
        client.println(F("    let phaseAcc = 0;"));
        
        // Draw Modulated Signal
        client.println(F("    for(let x=0; x<=w; x++) {"));
        client.println(F("      let bitIdx = Math.floor(x / pxPerBit);"));
        client.println(F("      if(bitIdx >= bits.length) bitIdx = bits.length-1;"));
        client.println(F("      let bit = bits[bitIdx] === '1';"));
        
        client.println(F("      let freq = 0.2; let amp = h/3; let phOff = 0;"));
        
        client.println(F("      if(modT === 'ASK') { amp = bit ? h/3 : h/12; freq=0.4; }")); // High amp vs Low amp
        client.println(F("      else if(modT === 'FSK') { freq = bit ? 0.6 : 0.2; }")); // High Freq vs Low Freq
        client.println(F("      else if(modT === 'PSK') { freq=0.3; if(!bit) phOff = Math.PI; }")); // Phase shift
        
        // Continuous phase accumulation for FSK/ASK, PSK uses direct phase
        client.println(F("      if(modT === 'PSK') { phaseAcc = (x * freq); } else { phaseAcc += freq; }")); 
        
        client.println(F("      let y = mid + Math.sin(phaseAcc + phOff) * amp;"));
        client.println(F("      if(x==0) ctx.moveTo(x,y); else ctx.lineTo(x,y);"));
        client.println(F("    }"));
        
        // Draw Bit Separators
        client.println(F("    ctx.stroke(); ctx.beginPath(); ctx.strokeStyle = 'rgba(255,255,255,0.2)';"));
        client.println(F("    for(let i=1; i<bits.length; i++) {"));
        client.println(F("      let bx = i * pxPerBit; ctx.moveTo(bx, 0); ctx.lineTo(bx, h);"));
        client.println(F("    }"));
        
        // Draw Bit Labels (0 or 1)
        client.println(F("    ctx.fillStyle = '#fff'; ctx.font='12px monospace';"));
        client.println(F("    for(let i=0; i<bits.length; i++) {"));
        client.println(F("       ctx.fillText(bits[i], (i*pxPerBit) + (pxPerBit/2) - 4, 15);"));
        client.println(F("    }"));
        client.println(F("  }"));
        
        client.println(F("  ctx.stroke();"));
        
        // Send to Arduino (Debounced)
        client.println(F("  clearTimeout(timer);"));
        client.println(F("  let q = mode===0 ? ('&freq='+stdF+'&wave='+stdW) : ('&freq=1000&wave='+modT);"));
        client.println(F("  timer = setTimeout(() => fetch('/set?'+q), 200);"));
        client.println(F("}"));
        
        client.println(F("resize();"));
        client.println(F("</script></body></html>"));
        break;
      }
      if (c == '\n') currentLine = ""; else if (c != '\r') currentLine += c;
    }
  }
  client.stop();
}
