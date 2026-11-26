// =====================================================================
//                         CAJA MUSICAL CON ARDUINO
//  Juliana Sofia Perez, Sofia Zarate, Gabriela Cepeda.
//
//  Cada disco tiene un sticker RFID. Al acercarlo al lector RC522, el
//  DFPlayer reproduce la cancion de ese disco, la LCD muestra el nombre y
//  el volumen, el LED RGB se enciende con el color de la cancion y la
//  figura (servomotor) baila al ritmo del volumen.
//  La caja empieza "dormida" y se despierta cuando la fotorresistencia (LDR)
//  detecta la sombra de alguien que se acerca.
// =====================================================================


// ---------------------------------------------------------------------
//  1. Declaracion de librerias
// ---------------------------------------------------------------------
#include <SPI.h>                  // Comunicacion SPI con el lector RFID.
#include <MFRC522.h>              // Libreria del lector RFID RC522.
#include <SoftwareSerial.h>       // Crea un puerto serial extra para el DFPlayer.
#include <DFRobotDFPlayerMini.h>  // Libreria del reproductor DFPlayer Mini.
#include <Wire.h>                 // Comunicacion I2C con la pantalla LCD.
#include <LiquidCrystal_I2C.h>    // Libreria de la pantalla LCD 16x2 con modulo I2C.
#include <Servo.h>                // Libreria para controlar el servomotor.


// ---------------------------------------------------------------------
//  2. Tabla de discos
// ---------------------------------------------------------------------
// Cada disco guarda: el UID de su sticker RFID, el numero de la cancion en
// la microSD, el nombre que se muestra en la LCD y el color del LED RGB
// (rojo, verde, azul, cada uno de 0 a 255).
struct Disco {
  const char* uid;      // Codigo del sticker RFID.
  int cancion;          // Numero del archivo en la microSD (0001.mp3, 0002.mp3...).
  const char* nombre;   // Nombre que se muestra en la pantalla.
  int rojo;             // Color del LED RGB para esta cancion.
  int verde;
  int azul;
};

Disco discos[] = {
  { "41 0e 2B 16", 1, "Piel Canela - Andres Cepeda",         255,  80,   0 },  // tarjeta 1: naranja
  { "41 31 86 16", 2, "maggots for brains - Olivia Rodrigo", 150,   0, 255 },  // tarjeta 2: morado
  { "71 C4 9F 15", 3, "iPod Touch - Ninajirachi",              0, 200, 255 },  // tarjeta 3: azul claro
  { "E0 61 73 5F", 4, "Volcan - Jose Jose",                  255,   0,   0 },  // tarjeta 4: rojo
  { "83 CF D2 56", 5, "That Way - Katseye",                  255,   0, 120 },  // tarjeta 5: rosado
};

// Calcula automaticamente cuantos discos hay en la tabla.
int NUM_DISCOS = sizeof(discos) / sizeof(discos[0]);


// ---------------------------------------------------------------------
//  3. Declaracion de variables para los pines digitales
// ---------------------------------------------------------------------
int pinDFPlayerRX = 2;     // Llega el TX del DFPlayer.
int pinDFPlayerTX = 4;     // Va al RX del DFPlayer (con resistencia de 1 kOhm).
// LED RGB de ANODO comun: la pata larga va a 5V.
int pinRGB_R = 5;          // Terminal rojo del LED RGB (PWM, con 330 Ohm).
int pinRGB_G = 6;          // Terminal verde del LED RGB (PWM, con 330 Ohm).
int pinRGB_B = 3;          // Terminal azul del LED RGB (PWM, con 330 Ohm).
                           // OJO: no puede ir en el pin 2 porque ahi esta el DFPlayer.
int pinServo = 7;          // Senal del servomotor.
int pinPulsador = 8;       // Pulsador (usa la resistencia interna INPUT_PULLUP).
int pinRFID_RST = 9;       // RST del lector RFID.
int pinRFID_SDA = 10;      // SDA del lector RFID.
                           // MOSI = 11, MISO = 12, SCK = 13 los maneja la libreria SPI.


// ---------------------------------------------------------------------
//  4. Declaracion de variables para los pines analogos
// ---------------------------------------------------------------------
int pinPotenciometro = A0; // Pin central del potenciometro (volumen).
int pinLDR = A2;           // Fotorresistencia (detecta si alguien se acerca).
                           // Conexion: LDR entre A2 y GND, resistencia entre A2 y 5V.
                           // A4 (SDA) y A5 (SCL) los usa la pantalla LCD.


// ---------------------------------------------------------------------
//  5. Valores limite del sistema
// ---------------------------------------------------------------------
int VOLUMEN_MAX = 15;        // Volumen maximo del DFPlayer (el maximo posible es 30).
int CENTRO = 90;             // Posicion de reposo del servo en grados.
int AMPLITUD_MAX = 60;       // Grados que se mueve el servo a volumen maximo.
int UMBRAL_SOMBRA = 100;     // Cuanto debe SUBIR la lectura del LDR (respecto a la
                             // luz del salon) para decir que alguien esta cerca.
int VELOCIDAD_TEXTO = 400;   // Cada cuantos ms se corre el titulo una letra a la izquierda.
unsigned long TIEMPO_DORMIR = 20000;  // Si en 20 s nadie hace nada y no suena musica,
                                      // la caja se vuelve a dormir.

// ---------------------------------------------------------------------
//  6. Objetos de las librerias
// ---------------------------------------------------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);              // Pantalla en la direccion 0x27, 16 columnas y 2 filas.
MFRC522 rfid(pinRFID_SDA, pinRFID_RST);          // Lector RFID.
SoftwareSerial mp3(pinDFPlayerRX, pinDFPlayerTX); // Puerto serial para el DFPlayer.
DFRobotDFPlayerMini dfp;                         // Reproductor DFPlayer.
Servo figura;                                    // Servomotor que mueve la figura.


// ---------------------------------------------------------------------
//  7. Variables para guardar el estado del sistema
// ---------------------------------------------------------------------
int discoActual = -1;              // Posicion del disco en la tabla (-1 = no hay disco).
bool pausado = false;              // true si la cancion esta en pausa.
int volumenActual = -1;            // Volumen actual (de 0 a VOLUMEN_MAX).
unsigned long mensajeHasta = 0;    // Hasta cuando se muestra un mensaje temporal en la LCD.

bool cajaActiva = false;           // false = caja dormida, true = caja despierta.
int valLuz = 0;                    // Ultima lectura del LDR (0 a 1023).
int luzAmbiente = 0;               // Lectura del LDR al encender (luz normal del salon).
unsigned long ultimaActividad = 0; // Momento en que alguien hizo algo por ultima vez.
unsigned long ultimaLecturaLuz = 0;// Para leer el LDR cada 200 ms.

int posicionTexto = 0;             // Letra del titulo desde donde se empieza a escribir.
unsigned long ultimoMovimientoTexto = 0; // Momento en que se corrio el titulo por ultima vez.

bool servoActivo = false;          // true si el servo esta bailando.
bool servoLado = false;            // Lado hacia donde se mueve (izquierda o derecha).
unsigned long ultimoPasoServo = 0; // Momento del ultimo movimiento del servo.
unsigned long apagarServoEn = 0;   // Momento en que se desconecta el servo para que no vibre.


// =====================================================================
//  SETUP: se ejecuta una sola vez al encender
// =====================================================================
void setup() {

  // Inicia el monitor serial para ver mensajes de prueba.
  Serial.begin(9600);

  // Configuracion de los pines digitales como entradas o salidas.
  pinMode(pinPulsador, INPUT_PULLUP);
  pinMode(pinRGB_R, OUTPUT);
  pinMode(pinRGB_G, OUTPUT);
  pinMode(pinRGB_B, OUTPUT);
  
  ponerColor(0, 0, 0);   // LED apagado.

  // Lleva el servo al centro y luego lo desconecta para que no vibre en reposo.
  figura.attach(pinServo);
  figura.write(CENTRO);
  delay(400);
  figura.detach();

  // Inicia la pantalla LCD.
  lcd.init();
  lcd.backlight();
  lcdEscribir("Caja musical", "Iniciando...");

  // Inicia el lector RFID.
  SPI.begin();
  rfid.PCD_Init();

  // Inicia el DFPlayer. Si no responde a la primera, lo intenta otra vez.
  mp3.begin(9600);
  delay(1500);
  if (!dfp.begin(mp3, true, true)) {
    dfp.begin(mp3, false, true);
    delay(1500);
  }
  delay(200);

  // Lee el volumen inicial del potenciometro.
  leerPotenciometro(true);

  // Calibracion del LDR: mide la luz normal del salon al encender.
  // (No tapar el sensor mientras el Arduino arranca.)
  luzAmbiente = analogRead(pinLDR);
  Serial.print(F("Luz del salon (referencia): "));
  Serial.println(luzAmbiente);

  Serial.println(F("=== Caja musical lista ==="));
  Serial.println(F("Pulsador: corto = pausa/reanudar, 1 s = reiniciar. Volumen: potenciometro"));
  Serial.println(F("Monitor serie: p = pausa, r = reanudar, l = ver lectura del LDR"));

  // La caja empieza dormida hasta que alguien se acerque.
    dormirCaja();

}


// =====================================================================
//  LOOP: programa principal, se repite todo el tiempo
// =====================================================================
void loop() {

  revisarMonitorSerie();     // Comandos de prueba desde el computador.
  revisarLuz();              // Revisa si alguien se acerca (LDR).
  revisarSiDormir();         // Duerme la caja si nadie la esta usando.
  leerPulsador();            // Pausa / reanudar / reiniciar.
  leerPotenciometro(false);  // Volumen.
  revisarMensajeTemporal();  // Quita los mensajes temporales de la LCD.
  moverTitulo();             // Corre el nombre de la cancion hacia la izquierda.
  moverServo();              // Hace bailar la figura.
  actualizarLED();           // Pone el color del LED segun el estado.

  // Si no hay una tarjeta nueva frente al lector, vuelve a empezar el loop.
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  // Convierte el codigo de la tarjeta a texto (por ejemplo "41 0E 2B 16").
  char uid[32];
  uidATexto(uid);
  Serial.print(F("UID leido: "));
  Serial.println(uid);

  // Busca el disco en la tabla. Si no esta, devuelve -1.
  int i = buscarDisco(uid);

  // Condicion anidada: el disco solo se reproduce si la caja esta despierta
  // (LDR detecto a alguien) Y ADEMAS el disco esta registrado en la tabla.
  if (cajaActiva) {

    ultimaActividad = millis();   // Alguien esta usando la caja.

    if (i < 0) {
      // El disco no esta en la tabla.
      Serial.println(F("  -> Tarjeta NO registrada. Copia este UID en la tabla 'discos'."));
      lcdEscribir("Disco no", "registrado");
      mensajeHasta = millis() + 2000;
    } else if (i == discoActual) {
      // Es el mismo disco que ya esta sonando.
      Serial.println(F("  -> Ese disco ya esta sonando."));
    } else {
      // Es un disco nuevo: reproduce su cancion.
      discoActual = i;
      pausado = false;
      posicionTexto = 0;          // El titulo empieza a correrse desde el principio.
      dfp.play(discos[i].cancion);
      Serial.print(F("  -> Reproduciendo: "));
      Serial.println(discos[i].nombre);
      pantallaCancion();
    }

  } else {
    // La caja esta dormida: no reproduce nada.
    Serial.println(F("  -> La caja esta dormida. Acerca la mano al sensor de luz primero."));
  }

  // Termina la comunicacion con la tarjeta y espera un poco.
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(300);
}


// =====================================================================
//  SENSOR DE LUZ (LDR): despertar y dormir la caja
// =====================================================================

// Por conexión realizada, con menos luz la resistencia del LDR aumenta y la lectura SUBE.
// Entonces cuando alguien se acerca y le hace sombra, la lectura es mayor
// que la luz del salon.
void revisarLuz() {

  // Lee el LDR cada 200 ms (no hace falta leerlo en cada vuelta).
  if (millis() - ultimaLecturaLuz < 200) return;
  ultimaLecturaLuz = millis();

  valLuz = analogRead(pinLDR);

  // Si la lectura sube por encima de la luz del salon + el umbral (100),
  // hay sombra: alguien esta cerca.
  if (valLuz > luzAmbiente + UMBRAL_SOMBRA) {

    ultimaActividad = millis();   // Mientras haya alguien cerca, la caja sigue despierta.

    if (!cajaActiva) {
      Serial.print(F("Alguien se acerco. Luz: "));
      Serial.println(valLuz);
      despertarCaja();
    }
  }
}

// Si la caja esta despierta, no suena musica y pasaron 20 s sin que
// nadie haga nada, la caja se duerme.
void revisarSiDormir() {
  if (!USAR_LDR) return;     // Sin LDR la caja nunca se duerme.
  if (!cajaActiva) return;

  bool sonando = (discoActual >= 0) && !pausado;

  if (!sonando && millis() - ultimaActividad > TIEMPO_DORMIR) {
    Serial.println(F("Nadie esta usando la caja. A dormir."));
    dormirCaja();
  }
}

void despertarCaja() {
  cajaActiva = true;
  ultimaActividad = millis();
  lcd.backlight();          // Prende la luz de la pantalla.
  mensajeHasta = 0;
  pantallaReposo();         // "Acerca un disco".
}

void dormirCaja() {
  // Si habia una cancion en pausa, la detiene.
  if (discoActual >= 0) {
    dfp.stop();
  }
  discoActual = -1;
  pausado = false;
  cajaActiva = false;

  lcdEscribir("Caja dormida", "Acerca la mano");
  lcd.noBacklight();        // Apaga la luz de la pantalla.
  ponerColor(0, 0, 0);      // Apaga el LED.
}


// =====================================================================
//  LED RGB
// =====================================================================

// Enciende el LED RGB con el color indicado (cada valor de 0 a 255).
// El LED es de ANODO comun (pata larga a 5V): funciona al reves,
// 0 = maximo brillo y 255 = apagado. Por eso se resta cada valor de 255,
// asi en el resto del codigo se sigue usando 0 = apagado y 255 = maximo.
void ponerColor(int r, int g, int b) {
  analogWrite(pinRGB_R, 255 - r);
  analogWrite(pinRGB_G, 255 - g);
  analogWrite(pinRGB_B, 255 - b);
}

// Elige el color del LED segun el estado de la caja:
//  - Sonando una cancion: el color de esa cancion.
//  - En pausa, sin disco o con la caja dormida: apagado.
void actualizarLED() {

  // Condicion: la caja esta despierta, hay un disco y no esta en pausa.
  if (cajaActiva && discoActual >= 0 && !pausado) {
    ponerColor(discos[discoActual].rojo,
               discos[discoActual].verde,
               discos[discoActual].azul);
  } else {
    ponerColor(0, 0, 0);
  }
}


// =====================================================================
//  PANTALLA LCD
// =====================================================================

// Escribe dos lineas completas en la LCD. Rellena con espacios para
// borrar lo que habia antes sin usar lcd.clear() (que hace parpadear).
void lcdEscribir(const char* linea1, const char* linea2) {
  lcd.setCursor(0, 0);
  imprimirRelleno(linea1);
  lcd.setCursor(0, 1);
  imprimirRelleno(linea2);
}

// Imprime maximo 16 letras y completa con espacios hasta 16.
void imprimirRelleno(const char* texto) {
  int n = 0;
  while (texto[n] != '\0' && n < 16) {
    lcd.print(texto[n]);
    n++;
  }
  while (n < 16) {
    lcd.print(' ');
    n++;
  }
}

// Pantalla cuando la caja esta despierta pero no hay disco.
void pantallaReposo() {
  lcdEscribir("Caja musical", "Acerca un disco");
}

// Pantalla con disco: linea 1 = nombre de la cancion, linea 2 = estado.
void pantallaCancion() {
  lcd.setCursor(0, 0);
  imprimirRelleno(discos[discoActual].nombre);
  lineaEstado();
}

// Corre el nombre de la cancion una letra hacia la izquierda cada
// VELOCIDAD_TEXTO ms, para que se pueda leer completo en la linea 1.
// Cuando termina el nombre deja 3 espacios y vuelve a empezar.
void moverTitulo() {

  // Solo se mueve si la caja esta despierta y hay un disco sonando o en pausa.
  if (!cajaActiva || discoActual < 0) return;

  // Si se esta mostrando un mensaje temporal, no lo tapa.
  if (millis() < mensajeHasta) return;

  // Espera el tiempo de VELOCIDAD_TEXTO entre cada movimiento.
  if (millis() - ultimoMovimientoTexto < VELOCIDAD_TEXTO) return;
  ultimoMovimientoTexto = millis();

  const char* nombre = discos[discoActual].nombre;
  int largo = strlen(nombre);

  // Si el nombre cabe en la pantalla (16 letras o menos), no hace falta moverlo.
  if (largo <= 16) return;

  // Escribe las 16 letras que caben, empezando en posicionTexto.
  // Si se pasa del final del nombre, escribe espacios de separacion.
  lcd.setCursor(0, 0);
  for (int i = 0; i < 16; i++) {
    int letra = (posicionTexto + i) % (largo + 3);
    if (letra < largo) {
      lcd.print(nombre[letra]);
    } else {
      lcd.print(' ');
    }
  }

  // Avanza una letra. Cuando llega al final, vuelve a empezar.
  posicionTexto++;
  if (posicionTexto >= largo + 3) {
    posicionTexto = 0;
  }
}

// Linea 2: volumen y si esta sonando o en pausa.
void lineaEstado() {
  char linea[17];
  if (pausado) {
    snprintf(linea, sizeof(linea), "Vol:%-2d     Pausa", volumenActual);
  } else {
    snprintf(linea, sizeof(linea), "Vol:%-2d   Sonando", volumenActual);
  }
  lcd.setCursor(0, 1);
  imprimirRelleno(linea);
}

// Vuelve a dibujar la pantalla que corresponde segun el estado.
void refrescarPantalla() {
  if (millis() < mensajeHasta) return;   // Todavia se esta mostrando un mensaje temporal.

  if (!cajaActiva) {
    lcdEscribir("Caja dormida", "Acerca la mano");
  } else if (discoActual < 0) {
    pantallaReposo();
  } else {
    pantallaCancion();
  }
}

// Cuando se acaba el tiempo de un mensaje temporal, vuelve a la pantalla normal.
void revisarMensajeTemporal() {
  if (mensajeHasta != 0 && millis() >= mensajeHasta) {
    mensajeHasta = 0;
    refrescarPantalla();
  }
}


// =====================================================================
//  PULSADOR: corto = pausa/reanudar, largo (1 s) = reiniciar cancion
// =====================================================================
bool botonAntes = HIGH;               // Estado anterior del pulsador (HIGH = suelto).
unsigned long ultimoCambioBoton = 0;  // Para evitar rebotes del pulsador.
unsigned long inicioPresion = 0;      // Momento en que se empezo a presionar.
bool largoHecho = false;              // true si ya se hizo la accion de presion larga.

void leerPulsador() {
  bool ahora = digitalRead(pinPulsador);

  // Detecta un cambio en el pulsador (ignora cambios de menos de 30 ms = rebotes).
  if (ahora != botonAntes && millis() - ultimoCambioBoton > 30) {
    ultimoCambioBoton = millis();
    botonAntes = ahora;

    if (ahora == LOW) {
      // Se acaba de presionar: empieza a contar el tiempo.
      inicioPresion = millis();
      largoHecho = false;
    } else if (!largoHecho) {
      // Se solto antes de 1 s: presion corta.
      pausarOReanudar();
    }
  }

  // Si se mantiene presionado mas de 1 s: presion larga.
  if (botonAntes == LOW && !largoHecho && millis() - inicioPresion > 1000) {
    largoHecho = true;
    reiniciarCancion();
  }
}

void pausarOReanudar() {
  if (!cajaActiva) {
    Serial.println(F("La caja esta dormida."));
    return;
  }
  ultimaActividad = millis();

  if (discoActual < 0) {
    Serial.println(F("Primero pon un disco."));
    lcdEscribir("Primero pon", "un disco");
    mensajeHasta = millis() + 1500;
    return;
  }

  if (pausado) {
    dfp.start();
    pausado = false;
    Serial.println(F("Reanudar"));
  } else {
    dfp.pause();
    pausado = true;
    Serial.println(F("Pausa"));
  }
  lineaEstado();
}

void reiniciarCancion() {
  if (!cajaActiva) {
    Serial.println(F("La caja esta dormida."));
    return;
  }
  ultimaActividad = millis();

  if (discoActual < 0) {
    Serial.println(F("Primero pon un disco."));
    lcdEscribir("Primero pon", "un disco");
    mensajeHasta = millis() + 1500;
    return;
  }

  dfp.play(discos[discoActual].cancion);
  pausado = false;
  Serial.println(F("Reiniciando cancion"));
  lcd.setCursor(0, 1);
  imprimirRelleno("Reiniciando...");
  mensajeHasta = millis() + 1000;
}


// =====================================================================
//  POTENCIOMETRO: volumen
// =====================================================================
unsigned long ultimaLecturaPot = 0;

void leerPotenciometro(bool forzar) {

  // Lee el potenciometro cada 150 ms (salvo que se fuerce la lectura).
  if (!forzar && millis() - ultimaLecturaPot < 150) return;
  ultimaLecturaPot = millis();

  int lectura = analogRead(pinPotenciometro);

  // Divide el recorrido del potenciometro en partes iguales de 0 a VOLUMEN_MAX.
  // (Se usa 1024 y VOLUMEN_MAX + 1 para que si llegue al volumen maximo.)
  int nuevo = map(lectura, 0, 1024, 0, VOLUMEN_MAX + 1);

  // Solo cambia el volumen si el potenciometro se movio lo suficiente,
  // para que el numero no salte entre dos valores.
  bool cambio = (nuevo != volumenActual) &&
                (abs(lectura - map(volumenActual, 0, VOLUMEN_MAX, 0, 1023)) > 30);

  if (forzar || volumenActual < 0 || cambio) {
    volumenActual = nuevo;
    dfp.volume(volumenActual);
    Serial.print(F("Volumen: "));
    Serial.println(volumenActual);

    if (!forzar) {
      ultimaActividad = millis();   // Mover el volumen tambien cuenta como actividad.
    }
    if (discoActual >= 0 && millis() >= mensajeHasta) {
      lineaEstado();
    }
  }
}


// =====================================================================
//  RFID
// =====================================================================

// Convierte los bytes del UID de la tarjeta a texto, por ejemplo "41 0E 2B 16".
void uidATexto(char* salida) {
  salida[0] = '\0';
  for (byte i = 0; i < rfid.uid.size; i++) {
    char parte[4];
    sprintf(parte, (i == 0) ? "%02X" : " %02X", rfid.uid.uidByte[i]);
    strcat(salida, parte);
  }
}

// Busca el UID en la tabla de discos. Devuelve su posicion o -1 si no esta.
// strcasecmp compara sin importar mayusculas/minusculas ("0e" = "0E").
int buscarDisco(const char* uid) {
  for (int i = 0; i < NUM_DISCOS; i++) {
    if (strcasecmp(uid, discos[i].uid) == 0) return i;
  }
  return -1;
}


// =====================================================================
//  MONITOR SERIE (para pruebas): p = pausa, r = reanudar, l = ver LDR
// =====================================================================
void revisarMonitorSerie() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if ((c == 'p' && !pausado) || (c == 'r' && pausado)) {
    pausarOReanudar();
  }

  // Muestra la lectura del LDR para ajustar UMBRAL_SOMBRA si hace falta.
  if (c == 'l') {
    Serial.print(F("LDR ahora: "));
    Serial.print(analogRead(pinLDR));
    Serial.print(F(" | Luz del salon: "));
    Serial.print(luzAmbiente);
    Serial.print(F(" | Se despierta por encima de: "));
    Serial.println(luzAmbiente + UMBRAL_SOMBRA);
  }
}


// =====================================================================
//  SERVOMOTOR: a mas volumen, se mueve mas grados y mas rapido
// =====================================================================
void moverServo() {

  // La figura solo baila si hay disco, no esta en pausa y el volumen no es 0.
  bool debeBailar = (discoActual >= 0) && !pausado && (volumenActual > 0);

  if (!debeBailar) {
    // Si estaba bailando, la regresa al centro.
    if (servoActivo) {
      figura.write(CENTRO);
      servoActivo = false;
      apagarServoEn = millis() + 400;
    }
    // Despues de 400 ms la desconecta para que no vibre ni gaste corriente.
    if (apagarServoEn != 0 && millis() >= apagarServoEn) {
      figura.detach();
      apagarServoEn = 0;
    }
    return;
  }

  // Si va a empezar a bailar, conecta el servo.
  if (!servoActivo) {
    figura.attach(pinServo);
    servoActivo = true;
    apagarServoEn = 0;
    ultimoPasoServo = 0;
  }

  // Volumen -> velocidad: entre 600 ms (volumen bajo) y 200 ms (volumen alto).
  // Se usa millis() en vez de delay() para que el resto del sistema siga funcionando.
  int intervalo = map(volumenActual, 1, VOLUMEN_MAX, 600, 200);
  if (millis() - ultimoPasoServo < (unsigned long)intervalo) return;  // Todavia no toca moverse.
  ultimoPasoServo = millis();

  // Volumen -> amplitud: entre 10 y 60 grados hacia cada lado.
  int amplitud = map(volumenActual, 1, VOLUMEN_MAX, 10, AMPLITUD_MAX);

  // Cambia de lado en cada paso: derecha, izquierda, derecha...
  servoLado = !servoLado;
  if (servoLado) {
    figura.write(CENTRO + amplitud);
  } else {
    figura.write(CENTRO - amplitud);
  }
}
