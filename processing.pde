import processing.serial.*;
import processing.data.*;
import java.text.SimpleDateFormat;
import java.util.Date;

// ====== Config ======
final int BAUD = 9600;
final int WORK_SECONDS   = 10;  // 25 min (ajusta aquí)
final int BREAK_SECONDS  = 1 * 60;  // 5  min  (ajusta aquí)
final int DIST_UMBRAL_MM = 600;
final int CONFIRM_COUNT  = 3;
final int BREAK_GRACE_SECONDS = 10;
final boolean DEBUG_LOG  = true;

// ====== Estados ======
final int IDLE        = 0;
final int WORK        = 1;
final int EARLY_LEAVE = 2;
final int BREAK       = 3;
int state = IDLE;

// ====== Serial ======
Serial myPort;
String lastPort = "";

// ====== Presencia (debounce) ======
boolean presence = false;
int presentStreak = 0;
int absentStreak  = 0;

// ====== Break ======
boolean breakPaused = false;
int breakPresenceHold = 0;

// ====== Timers ======
int workRemaining  = 0;
int breakRemaining = 0;
long lastTickMs    = 0;

// ====== UI ======
PFont font, fontMono;
SimpleDateFormat clockFmt = new SimpleDateFormat("HH:mm:ss");

// ====== Theme ======
int COL_BG           = color(245, 246, 248);
int COL_CARD         = color(255);
int COL_TEXT         = color(20);
int COL_MUTED        = color(110);
int COL_BORDER       = color(230);
int COL_IDLE         = color(120, 120, 120);
int COL_WORK         = color(46, 125, 50);    // verde
int COL_BREAK        = color(25, 118, 210);   // azul
int COL_EARLY_LEAVE  = color(230, 81, 0);     // naranja
int COL_WARN         = color(198, 40, 40);    // rojo
int SHADOW           = color(0, 0, 0, 18);

void setup() {
  size(760, 420);
  smooth(4);
  font = createFont("Inter", 16, true);
  fontMono = createFont("JetBrains Mono", 14, true);
  textFont(font);

  // Serial
  println("Puertos disponibles:");
  println(Serial.list());
  int idx = 1; // <-- AJUSTA el índice si es necesario
  String portName = Serial.list().length > idx ? Serial.list()[idx] : "";
  if (portName.equals("")) {
    println("No hay puertos serie disponibles.");
  } else {
    myPort = new Serial(this, portName, BAUD);
    myPort.clear();
    myPort.bufferUntil('\n');
    lastPort = portName;
  }

  enviarLCD("Esperando", "Acércate al sensor");
  lastTickMs = millis();
}

void draw() {
  background(COL_BG);
  drawTopBar();
  drawLayout();
  // Tick 1s
  long now = millis();
  if (now - lastTickMs >= 1000) {
    lastTickMs = now;
    tick1s();
  }
}

// ====== Top Bar (título + reloj) ======
void drawTopBar() {
  // título
  fill(COL_TEXT);
  textAlign(LEFT, CENTER);
  textSize(22);
  text("Sistema Trabajo / Descanso", 24, 28);

  // reloj
  String hhmmss = clockFmt.format(new Date());
  fill(COL_MUTED);
  textSize(16);
  textAlign(RIGHT, CENTER);
  text("Hora: " + hhmmss, width - 24, 28);
}

// ====== Main layout ======
void drawLayout() {
  // Panel izquierdo: estado, puerto, umbral, presencia
  float leftX = 24, leftY = 64, leftW = 340, leftH = height - leftY - 24;
  drawCard(leftX, leftY, leftW, leftH);

  // Estado chip + info
  String estadoStr =
    state == IDLE        ? "ESPERANDO" :
    state == WORK        ? "TRABAJO"   :
    state == EARLY_LEAVE ? "VOLVER A TRABAJAR" :
                           "DESCANSO";
  int estadoCol =
    state == IDLE        ? COL_IDLE :
    state == WORK        ? COL_WORK :
    state == BREAK       ? COL_BREAK :
                           COL_EARLY_LEAVE;

  drawChip(leftX + 16, leftY + 16, estadoStr, estadoCol);

  // Datos clave
  float y = leftY + 16 + 36;
  drawKVP("Puerto", lastPort.equals("") ? "(sin puerto)" : lastPort, leftX + 16, y); y += 28;
  drawKVP("Umbral", DIST_UMBRAL_MM + " mm", leftX + 16, y); y += 28;
  drawKVP("Presencia", presence ? "Sí" : "No", leftX + 16, y, presence ? estadoCol : COL_MUTED); y += 28;

  // Barra de progreso horizontal (alternativa al anillo)
  String t =
    (state == WORK || state == EARLY_LEAVE) ? formatoTiempo(workRemaining) :
    (state == BREAK) ? formatoTiempo(breakRemaining) : "--:--";

  int total = (state == BREAK) ? BREAK_SECONDS : ((state == WORK || state == EARLY_LEAVE) ? WORK_SECONDS : 0);
  int remaining = (state == BREAK) ? breakRemaining : ((state == WORK || state == EARLY_LEAVE) ? workRemaining : 0);
  float pct = (total > 0) ? 1.0 - (remaining / max(1.0, float(total))) : 0;

  y += 20;
  drawSectionTitle("Progreso", leftX + 16, y); y += 10;
  drawProgressBar(leftX + 16, y, leftW - 32, 14, pct, estadoCol);
  y += 28;
  drawMono("Tiempo restante: " + t, leftX + 16, y);
  y += 24;

  // Mensajitos de break pausado o early leave
  if (state == BREAK && breakPaused) {
    drawNote("Descanso pausado por presencia", leftX + 16, y, COL_WARN);
    y += 24;
  }
  if (state == EARLY_LEAVE) {
    drawNote("Trabajo en pausa: regresa para continuar", leftX + 16, y, COL_EARLY_LEAVE);
    y += 24;
  }

  // Panel derecho: Timer central con anillo
  float rightX = leftX + leftW + 20;
  float rightY = leftY;
  float rightW = width - rightX - 24;
  float rightH = leftH;
  drawCard(rightX, rightY, rightW, rightH);

  // Timer circular
  float cx = rightX + rightW/2;
  float cy = rightY + rightH/2 - 10;
  float radius = min(rightW, rightH) * 0.42;

  drawTimerCircle(cx, cy, radius, remaining, total, estadoCol);
  // Etiquetas
  textAlign(CENTER, CENTER);
  fill(COL_TEXT);
  textSize(16);
  text("Estado: " + estadoStr, cx, cy + radius + 22);
}

// ====== Cards / Shapes ======
void drawCard(float x, float y, float w, float h) {
  noStroke();
  fill(SHADOW); // sombrita
  rect(x + 3, y + 4, w, h, 16);
  fill(COL_CARD);
  stroke(COL_BORDER);
  strokeWeight(1);
  rect(x, y, w, h, 16);
}

void drawChip(float x, float y, String label, int col) {
  textAlign(LEFT, CENTER);
  textSize(13);
  float tw = textWidth(label) + 20;
  noStroke();
  fill(col);
  rect(x, y, tw, 28, 999);
  fill(255);
  text(label, x + 10, y + 14);
}

void drawKVP(String k, String v, float x, float y) {
  drawKVP(k, v, x, y, COL_TEXT);
}

void drawKVP(String k, String v, float x, float y, int vcol) {
  fill(COL_MUTED);
  textAlign(LEFT, CENTER);
  textSize(14);
  text(k, x, y + 10);
  fill(vcol);
  textSize(15);
  text(v, x + max(100, textWidth(k) + 16), y + 10);
}

void drawSectionTitle(String t, float x, float y) {
  fill(COL_MUTED);
  textAlign(LEFT, CENTER);
  textSize(13);
  text(t, x, y);
}

void drawMono(String t, float x, float y) {
  textFont(fontMono);
  fill(COL_TEXT);
  textAlign(LEFT, CENTER);
  textSize(14);
  text(t, x, y);
  textFont(font);
}

void drawNote(String t, float x, float y, int c) {
  noStroke();
  fill(c, 24);
  rect(x - 6, y - 14, textWidth(t) + 24, 28, 10);
  fill(c);
  textAlign(LEFT, CENTER);
  textSize(14);
  text(t, x + 6, y);
}

void drawProgressBar(float x, float y, float w, float h, float pct, int col) {
  stroke(COL_BORDER);
  strokeWeight(1);
  fill(255);
  rect(x, y, w, h, 8);
  noStroke();
  fill(col);
  float fillW = constrain(w * pct, 0, w);
  rect(x, y, fillW, h, 8);
}

// ====== Timer circular ======
void drawTimerCircle(float cx, float cy, float r, int remaining, int total, int col) {
  // base
  noFill();
  stroke(COL_BORDER);
  strokeWeight(16);
  ellipse(cx, cy, r*2, r*2);

  // arco
  float pct = (total > 0) ? 1.0 - (remaining / max(1.0, float(total))) : 0;
  stroke(col);
  strokeWeight(16);
  strokeCap(SQUARE);
  float start = -HALF_PI;                 // inicia arriba
  float sweep = TWO_PI * pct;
  arc(cx, cy, r*2, r*2, start, start + sweep);

  // texto central (tiempo grande)
  String timeStr =
    (state == WORK || state == EARLY_LEAVE) ? formatoTiempo(remaining) :
    (state == BREAK) ? formatoTiempo(remaining) : "--:--";
  fill(COL_TEXT);
  textAlign(CENTER, CENTER);
  textSize(44);
  text(timeStr, cx, cy - 4);

  // subtexto
  String sub =
    state == WORK ? "Tiempo de trabajo" :
    state == BREAK ? (breakPaused ? "Descanso (pausado)" : "Tiempo de descanso") :
    state == EARLY_LEAVE ? "Regresa para continuar" :
    "Acércate para iniciar";
  fill(COL_MUTED);
  textSize(14);
  text(sub, cx, cy + 28);
}

// ====== Tiqueo de 1s según estado ======
void tick1s() {
  switch (state) {
    case WORK:
      if (presence && workRemaining > 0) {
        workRemaining--;
        if (workRemaining % 5 == 0 || workRemaining < 10) {
          enviarLCD("Trabaja", "Restan " + formatoTiempo(workRemaining));
        }
        if (workRemaining == 0) {
          enviarLCD("Descanso", "Alejate");
          enviarBuzzer(200, 200, 2);
          // esperamos ausencia confirmada para pasar a BREAK
        }
      }
      break;

    case EARLY_LEAVE:
      // pausado
      break;

    case BREAK:
      if (presence) {
        if (breakPresenceHold < BREAK_GRACE_SECONDS) {
          breakPresenceHold++;
          if (breakPresenceHold == BREAK_GRACE_SECONDS) {
            breakPaused = true;
            enviarLCD("Descanso pausado", formatoTiempo(breakRemaining));
            enviarBuzzerAlarma();
          }
        }
      }
      if (!breakPaused && breakRemaining > 0) {
        breakRemaining--;
        if (breakRemaining % 5 == 0 || breakRemaining < 10) {
          enviarLCD("Descanso", "Restan " + formatoTiempo(breakRemaining));
        }
        if (breakRemaining == 0) {
          enviarLCD("Listo", "¡Vamos!");
          enviarBuzzer(150, 0, 1);
          state = IDLE;
        }
      }
      break;

    case IDLE:
    default:
      break;
  }
}

// ====== Serial: recepción de Arduino ======
void serialEvent(Serial p) {
  String input = p.readStringUntil('\n');
  if (input == null) return;
  input = trim(input);
  if (input.length() == 0) return;

  //if (DEBUG_LOG) println("IN: " + input);

  try {
    JSONObject msg = parseJSONObject(input);
    if (msg == null) return;

    String type = msg.getString("type");

    if (type.equals("SENSOR_READING")) {
      int d  = msg.getInt("d_mm");
      boolean ok = msg.getBoolean("ok");
      //if (DEBUG_LOG) println("d_mm=" + d + " ok=" + ok);
      if (ok) actualizarPresenciaConDebounce(d);
    }
    else if (type.equals("READY")) {
      enviarLCD("Listo", "Esperando...");
    }
    else if (type.equals("HEARTBEAT")) {
      // opcional: monitoreo
    }
    else if (type.equals("ERROR")) {
      String code = msg.getString("code");
      enviarLCD("Error sensor", code);
    }
  }
  catch (Exception e) {
    println("Error parseando JSON: " + e);
  }
}

// ====== Presencia con debounce + transición de estados ======
void actualizarPresenciaConDebounce(int d_mm) {
  boolean nowPresent = (d_mm > 0 && d_mm < DIST_UMBRAL_MM);

  if (nowPresent) {
    presentStreak++;
    absentStreak = 0;
    if (!presence && presentStreak >= CONFIRM_COUNT) {
      presence = true;
      onPresenceConfirmed();
    }
  } else {
    absentStreak++;
    presentStreak = 0;
    if (presence && absentStreak >= CONFIRM_COUNT) {
      presence = false;
      onAbsenceConfirmed();
    }
  }
}

// ====== Transiciones por presencia confirmada ======
void onPresenceConfirmed() {
  if (DEBUG_LOG) println(">> Presencia confirmada");

  switch (state) {
    case IDLE:
      startWork();
      break;

    case EARLY_LEAVE:
      enviarReset();
      enviarLCD("Trabaja", "Restan " + formatoTiempo(workRemaining));
      state = WORK;
      break;

    case BREAK:
      enviarLCD("Aun descanso", "Quedan " + formatoTiempo(breakRemaining));
      enviarBuzzer(300, 300, 3);
      break;

    case WORK:
    default:
      break;
  }
}

// ====== Transiciones por ausencia confirmada ======
void onAbsenceConfirmed() {
  if (DEBUG_LOG) println(">> Ausencia confirmada");

  switch (state) {
    case WORK:
      if (workRemaining > 0) {
        state = EARLY_LEAVE;
        enviarLCD("Aun trabajo", "Quedan " + formatoTiempo(workRemaining));
        enviarBuzzerAlarma();
      } else {
        startBreak();
      }
      break;

    case EARLY_LEAVE:
      // sin cambios
      break;

    case IDLE:
      break;

    case BREAK:
      if (breakPaused) {
        breakPaused = false;
        enviarReset();
        enviarLCD("Descanso", "Restan " + formatoTiempo(breakRemaining));
      }
      breakPresenceHold = 0;
      break;
  }
}

// ====== Helpers de estados ======
void startWork() {
  workRemaining = WORK_SECONDS;
  state = WORK;
  enviarLCD("Trabaja", "Restan " + formatoTiempo(workRemaining));
}

void startBreak() {
  breakRemaining = BREAK_SECONDS;
  state = BREAK;
  enviarLCD("Descanso", "Restan " + formatoTiempo(breakRemaining));
}

// ====== Envío de comandos a Arduino ======
void enviarLCD(String l1, String l2) {
  if (myPort == null) return;
  JSONObject msg = new JSONObject();
  msg.setString("src", "P");
  msg.setString("type", "LCD_TEXT");
  msg.setString("l1", l1);
  msg.setString("l2", l2);

  String payload = msg.toString().replace("\n","").replace("\r","");
  String line = payload + "\n";
  if (DEBUG_LOG) println("OUT: " + line);
  myPort.write(line);
}

  void enviarBuzzerAlarma() {
  enviarBuzzer(300, 300, 0);
}

void enviarBuzzer(int on_ms, int off_ms, int repeat) {
  if (myPort == null) return;
  JSONObject msg = new JSONObject();
  msg.setString("src", "P");
  msg.setString("type", "BUZZER_DRIVE");
  msg.setString("mode", "ON_OFF");
  msg.setInt("on_ms", on_ms);
  msg.setInt("off_ms", off_ms);
  msg.setInt("repeat", repeat);

  String payload = msg.toString().replace("\n","").replace("\r","");
  String line = payload + "\n";
  if (DEBUG_LOG) println("OUT: " + line);
  myPort.write(line);
}

void enviarReset() {
  if (myPort == null) return;
  JSONObject msg = new JSONObject();
  msg.setString("src", "P");
  msg.setString("type", "RESET");
  String payload = msg.toString().replace("\n","").replace("\r","");
  String line = payload + "\n";
  if (DEBUG_LOG) println("OUT: " + line);
  myPort.write(line);
}

// ====== Util ======
String formatoTiempo(int s) {
  if (s < 0) s = 0;
  int m = s / 60;
  int ss = s % 60;
  return nf(m, 2) + ":" + nf(ss, 2);
}
