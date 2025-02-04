#include <ESP8266WiFi.h>          // Biblioteca para manejar WiFi
#include <ESP8266WebServer.h>     // Biblioteca para crear un servidor web
#include <EEPROM.h>              // Biblioteca para almacenamiento persistente
#include <PubSubClient.h>        // Biblioteca para MQTT
#include <WiFiClientSecure.h>    // Biblioteca para conexiones seguras (TLS)

// Variables globales
const int ledPin = 2; // GPIO2 (D4) en el WEMOS D1 Mini Pro
bool ledState = false;

// Estado del modo AP
bool apMode = true; // Comienza en modo AP
String savedSSID = ""; // Red WiFi guardada
String savedPassword = ""; // Contraseña de la red WiFi guardada

// Configuración MQTT
const char* mqttServer = "28145d014f47483eb6cd996612566cf7.s1.eu.hivemq.cloud"; // URL del broker HiveMQ
const int mqttPort = 8883; // Puerto TLS
const char* mqttUser = "JaviLed"; // Reemplaza con tu usuario HiveMQ
const char* mqttPassword = "Javi1234"; // Reemplaza con tu contraseña HiveMQ
const char* mqttTopic = "wemosLed"; // Tema MQTT para controlar el LED

// Inicialización del servidor web en el puerto 80
ESP8266WebServer server(80);

// Cliente MQTT seguro
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Función para leer datos desde EEPROM
void readFromEEPROM() {
  String ssid = "";
  String password = "";

  // Leer SSID (primeros 32 bytes)
  for (int i = 0; i < 32; i++) {
    ssid += char(EEPROM.read(i));
  }

  // Leer contraseña (siguientes 32 bytes)
  for (int i = 32; i < 64; i++) {
    password += char(EEPROM.read(i));
  }

  // Verificar si los datos son válidos
  if (ssid != "" && password != "") {
    savedSSID = ssid;
    savedPassword = password;
    Serial.println("Datos leídos de EEPROM:");
    Serial.println("SSID: " + savedSSID);
    Serial.println("Contraseña: " + savedPassword);
    apMode = false; // Salir del modo AP si hay datos guardados
  } else {
    Serial.println("No se encontraron datos en EEPROM.");
  }
}

// Función para escribir datos en EEPROM
void writeToEEPROM(String ssid, String password) {
  // Guardar SSID (primeros 32 bytes)
  for (int i = 0; i < ssid.length() && i < 32; i++) {
    EEPROM.write(i, ssid[i]);
  }
  for (int i = ssid.length(); i < 32; i++) {
    EEPROM.write(i, '\0'); // Rellenar con caracteres nulos
  }

  // Guardar contraseña (siguientes 32 bytes)
  for (int i = 0; i < password.length() && i < 32; i++) {
    EEPROM.write(32 + i, password[i]);
  }
  for (int i = password.length(); i < 32; i++) {
    EEPROM.write(32 + i, '\0'); // Rellenar con caracteres nulos
  }

  EEPROM.commit(); // Guardar cambios en la memoria
  Serial.println("Datos guardados en EEPROM.");
}

// Función para escanear redes WiFi disponibles
void scanWiFiNetworks() {
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No se encontraron redes WiFi.");
  } else {
    Serial.print("Se encontraron ");
    Serial.print(n);
    Serial.println(" redes WiFi:");
    for (int i = 0; i < n; ++i) {
      Serial.printf("  %d: %-32s %ddBm\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
  }
}

// Función para manejar la página principal en modo AP
void handleConfigPage() {
  String htmlResponse = "<html><head><title>Configuración de WiFi</title></head><body>";
  htmlResponse += "<h1>Selecciona una red WiFi</h1>";

  // Escanear redes WiFi disponibles
  int n = WiFi.scanNetworks();
  if (n == 0) {
    htmlResponse += "<p>No se encontraron redes WiFi disponibles.</p>";
  } else {
    htmlResponse += "<form action='/connect' method='get'>";
    htmlResponse += "<select name='ssid'>";
    for (int i = 0; i < n; ++i) {
      htmlResponse += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
    }
    htmlResponse += "</select>";
    htmlResponse += "<label for='password'>Contraseña:</label>";
    htmlResponse += "<input type='text' name='password'>";
    htmlResponse += "<input type='submit' value='Conectar'>";
    htmlResponse += "</form>";
  }

  htmlResponse += "</body></html>";
  server.send(200, "text/html", htmlResponse);
}

// Función para manejar la conexión a una red WiFi
void handleConnectWiFi() {
  if (server.args() > 0) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    // Intentar conectar a la red WiFi seleccionada
    WiFi.mode(WIFI_STA); // Modo cliente
    WiFi.begin(ssid.c_str(), password.c_str());
    int connectTimeout = 20; // Tiempo máximo de espera (en segundos)
    while (WiFi.status() != WL_CONNECTED && connectTimeout-- > 0) {
      delay(1000);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConexión exitosa!");
      server.send(200, "text/plain", "Conexión exitosa! Ahora puedes acceder al control del LED.");

      // Guardar datos en EEPROM
      savedSSID = ssid;
      savedPassword = password;
      writeToEEPROM(savedSSID, savedPassword);

      apMode = false; // Salir del modo AP
    } else {
      Serial.println("\nFallo al conectar. Regresando al modo AP...");
      server.send(200, "text/plain", "Fallo al conectar. Por favor, inténtalo de nuevo.");
    }
  } else {
    server.send(200, "text/plain", "Parámetros incorrectos. Por favor, selecciona una red WiFi válida.");
  }
}

// Función para manejar la página principal en modo STA
void handleControlPage() {
  String htmlResponse = "<html><head><title>Control de LED</title>";
  htmlResponse += "<script>";
  htmlResponse += "function toggleLED() {";
  htmlResponse += "  var xhr = new XMLHttpRequest();";
  htmlResponse += "  xhr.open('GET', '/led/toggle', true);";
  htmlResponse += "  xhr.onload = function() {";
  htmlResponse += "    if (xhr.responseText == 'ON') {";
  htmlResponse += "      document.getElementById('ledStatus').innerText = 'LED ENCENDIDO';";
  htmlResponse += "      document.getElementById('ledButton').innerText = 'Apagar LED';";
  htmlResponse += "    } else {";
  htmlResponse += "      document.getElementById('ledStatus').innerText = 'LED APAGADO';";
  htmlResponse += "      document.getElementById('ledButton').innerText = 'Encender LED';";
  htmlResponse += "    }";
  htmlResponse += "  };";
  htmlResponse += "  xhr.send();";
  htmlResponse += "}";
  htmlResponse += "</script>";
  htmlResponse += "</head><body>";

  // Concatenación corregida para el estado del LED
  htmlResponse += "<p id='ledStatus'>Estado del LED: ";
  htmlResponse += (ledState ? "ENCENDIDO" : "APAGADO");
  htmlResponse += "</p>";

  // Concatenación corregida para el botón
  htmlResponse += "<button id='ledButton' onclick='toggleLED()' style='font-size:24px;'>";
  htmlResponse += (ledState ? "Apagar LED" : "Encender LED");
  htmlResponse += "</button>";

  htmlResponse += "</body></html>";

  server.send(200, "text/html", htmlResponse);
}

// Función para alternar el estado del LED
void handleLedToggle() {
  ledState = !ledState; // Cambiar el estado del LED
  digitalWrite(ledPin, ledState ? LOW : HIGH); // Encender/Apagar el LED
  server.send(200, "text/plain", ledState ? "ON" : "OFF"); // Confirmar acción

  // Publicar estado del LED en MQTT
  String payload = ledState ? "ON" : "OFF";
  client.publish(mqttTopic, payload.c_str());
}

// Conexión MQTT
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Intentando conectar a MQTT...");
    if (client.connect("WEMOS_D1_MINI", mqttUser, mqttPassword)) {
      Serial.println("Conexión exitosa!");
      client.subscribe(mqttTopic); // Suscribirse al tema
    } else {
      Serial.print("Fallo. Reintentando en 5 segundos...");
      delay(5000);
    }
  }
}

// Callback MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (message == "ON") {
    ledState = true;
    digitalWrite(ledPin, LOW); // Encender LED
  } else if (message == "OFF") {
    ledState = false;
    digitalWrite(ledPin, HIGH); // Apagar LED
  }
}

// Configuración inicial
void setup() {
  // Iniciar el LED como salida
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // Apagar el LED inicialmente
  ledState = false;

  // Iniciar la comunicación serial para depuración
  Serial.begin(9600);
  Serial.println("Iniciando...");

  // Iniciar EEPROM
  EEPROM.begin(512); // Reservar espacio en la memoria EEPROM
  readFromEEPROM(); // Leer datos guardados

  // Configurar el punto de acceso WiFi o conectar a la red guardada
  if (apMode) {
    WiFi.mode(WIFI_AP); // Modo AP
    WiFi.softAP("MiPuntoAcceso", "12345678"); // Crear el punto de acceso
    Serial.println("Modo AP iniciado.");
    Serial.print("IP del servidor: ");
    Serial.println(WiFi.softAPIP()); // Mostrar la IP del AP
  } else {
    WiFi.mode(WIFI_STA); // Modo cliente
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    int connectTimeout = 20; // Tiempo máximo de espera (en segundos)
    while (WiFi.status() != WL_CONNECTED && connectTimeout-- > 0) {
      delay(1000);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConexión exitosa!");
    } else {
      Serial.println("\nFallo al conectar. Regresando al modo AP...");
      apMode = true;
      WiFi.mode(WIFI_AP);
      WiFi.softAP("MiPuntoAcceso", "12345678");
    }
  }

  // Configurar las rutas del servidor web
  server.on("/", []() {
    if (apMode) {
      handleConfigPage(); // Página de configuración en modo AP
    } else {
      handleControlPage(); // Página de control en modo STA
    }
  });

  server.on("/connect", handleConnectWiFi); // Ruta para conectar a una red WiFi
  server.on("/led/toggle", handleLedToggle); // Alternar el LED

  // Iniciar el servidor web
  server.begin();
  Serial.println("Servidor iniciado.");

  // Configurar MQTT
  espClient.setInsecure(); // Desactivar verificación de certificados (solo para pruebas)
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
}

// Bucle principal
void loop() {
  if (apMode) {
    server.handleClient(); // Manejar solicitudes del cliente en modo AP
  } else {
    if (WiFi.status() == WL_CONNECTED) {
      server.handleClient(); // Manejar solicitudes del cliente en modo STA

      // Mantener conexión MQTT
      if (!client.connected()) {
        reconnectMQTT();
      }
      client.loop();
    } else {
      // Si se desconecta, regresar al modo AP
      Serial.println("Perdida de conexión. Regresando al modo AP...");
      apMode = true;
      WiFi.mode(WIFI_AP);
      WiFi.softAP("MiPuntoAcceso", "12345678");
    }
  }
}
