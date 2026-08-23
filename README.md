# Control Jacuzzi ESP32 (DevKit / NodeMCU-32S)

Proyecto completo: control de bomba, valvulas de desvio solar, sensores
de temperatura, programacion horaria y web de control en tiempo real.

## Estructura del proyecto

```
esp32_jacuzzi/
├── esp32_jacuzzi.ino   -> Solo llama a setup/loop de cada modulo
├── config.h            -> Pines y constantes (LEER ANTES DE CABLEAR)
├── data.h              -> Estado global compartido (g_state)
├── storage.*           -> Guardado persistente (NVS / Preferences)
├── wifi_manager.*       -> WiFi: redes conocidas + portal de configuracion
├── relays.*             -> Control de bomba y valvulas
├── temp_sensors.*       -> Lectura de los 2 DS18B20
├── schedule.*            -> Programa horario + logica de modo automatico
├── ota.*                 -> Actualizacion de firmware por WiFi
├── web_server.*          -> Servidor web + WebSocket
└── webpage.*             -> HTML de la app embebido en PROGMEM (sin filesystem)
```

## Librerias necesarias (Gestor de Librerias del IDE de Arduino)

- **ESPAsyncWebServer** (me-no-dev / ESP32Async)
- **AsyncTCP** (dependencia de la anterior)
- **ArduinoJson** (v6 o v7)
- Todo lo demas (WiFi, WebServer, DNSServer, Preferences, ArduinoOTA,
  analogRead de los NTC) ya viene incluido con el core de ESP32 para
  Arduino, sin librerias externas adicionales.

## Configuracion de la placa

- Placa: **ESP32 DevKit / NodeMCU-32S**
- Particion: cualquiera por defecto sirve (ya no se usa ningun sistema
  de ficheros; la web viaja embebida dentro del propio sketch).
- Sube el sketch normalmente por USB, como cualquier otro proyecto de
  Arduino. No hay que subir nada aparte.

## Cableado (ver comentarios detallados en `config.h`)

| Señal                          | Pin ESP32 DevKit |
|---------------------------------|:---:|
| NTC T1 (jacuzzi)                | GPIO 32 |
| NTC T2 (solar)                  | GPIO 33 |
| Rele 1 - Bomba/Motor            | GPIO 26 |
| Rele 2 - Valvula V1             | GPIO 27 |
| Rele 3 - Valvula V2             | GPIO 14 |
| Rele 4 - Libre / futuro         | GPIO 25 |
| Boton reset WiFi (boton BOOT)   | GPIO 0  |
| LED de estado (LED azul on-board) | GPIO 2  |

Si tu modulo de reles es activo en HIGH en vez de en LOW, cambia
`RELAY_ACTIVE_LOW` a `false` en `config.h`.

**Sensores NTC10k B3950 (analogicos, 2 hilos, sin polaridad):** cada uno
va en un divisor de tension 3.3V — resistencia fija de 10kΩ — NTC — GND,
y el ESP32 lee el nodo intermedio. GPIO32 y GPIO33 son del ADC1, que a
diferencia del ADC2 SI es fiable con el WiFi activo, asi que no hay
ninguna limitacion de hardware que tener en cuenta aqui.

## Conexion WiFi: captive portal bajo demanda

Para no dejar una puerta de entrada abierta 24/7 (este ESP32 controla
reles de bomba y valvulas), el punto de acceso de configuracion NO es
permanente, pero es un **captive portal completo**: al conectarte a el,
tu movil/PC abre la pagina de configuracion solo, como el WiFi de un
bar o un hotel (sin tener que teclear ninguna direccion).

1. **En cada arranque**, el ESP32 abre automaticamente durante
   **5 minutos** (`AP_CONFIG_WINDOW_MS` en `config.h`) la red
   **`Jacuzzi-Config`** (password `12345678`) como red de seguridad,
   mientras intenta conectarse en paralelo a sus redes conocidas.
2. Si logra conectar antes de que pasen esos 5 minutos, el AP **se
   cierra solo**. Si nunca logra conectar a ninguna, se queda abierto
   **indefinidamente** (es la unica via de entrada que le queda).
3. **Bajo demanda**: en la app web hay un boton **CONFIGURAR WIFI** que
   activa el mismo captive portal en cualquier momento, SIN reiniciar
   el ESP32 (usa el modo dual AP+STA: sigue conectado a tu red de casa
   mientras el AP de configuracion esta activo en paralelo).
4. Al conectarte a `Jacuzzi-Config`, la pagina de gestion se abre sola
   y permite:
   - **Escanear y añadir** redes nuevas.
   - **Priorizar** las redes guardadas con las flechas ▲▼ (la de arriba
     del todo es la que se intenta conectar primero si esta visible;
     ya NO se elige por intensidad de señal, sino por el orden que tu
     decidas).
   - **Eliminar** redes guardadas.
   - **Guardar y salir**: reinicia el ESP32 para aplicar los cambios y
     reconectarse con la nueva configuracion.

Si tras publicar el proyecto en tu red algun movil no abre la pagina
de configuracion automaticamente al conectarse (varia segun fabricante
y version de Android/iOS), siempre puedes entrar manualmente a
`http://192.168.4.1`.

## Actualizacion OTA

Una vez el ESP32 esta en tu red, aparecera en el IDE de Arduino como
puerto de red (`jacuzzi-esp32`) para subir nuevo firmware sin cable.

## Monitor Serie (115200 baudios)

Todo el arranque y el funcionamiento imprime mensajes con un prefijo
por modulo para poder seguir lo que ocurre: `[MAIN]`, `[WIFI]`,
`[RELAYS]`, `[TEMP]`, `[SCHED]`, `[WEB]`, `[OTA]`. Por ejemplo, al
arrancar veras el orden de inicializacion, si conecto a una red
conocida o abrio el portal de configuracion, las lecturas de
temperatura cada `SENSOR_READ_MS`, los cambios de valvulas/modo, y
cuando un cliente web se conecta o envia un comando.

## Notas de diseño

- **Un unico servidor web** (`web_server.cpp`, puerto 80) sirve tanto la
  app normal como las rutas del captive portal de configuracion WiFi.
  Es importante que sea uno solo: dos servidores distintos escuchando el
  mismo puerto 80 a la vez provocan conflictos de red (fue exactamente
  el bug de una version anterior: abrir la app estando ya conectado
  acababa mostrando la pagina de configuracion WiFi, porque el servidor
  equivocado respondia a la peticion). Las rutas del portal solo
  responden de verdad mientras el AP esta activo; si no, devuelven 404.

- La web esta **embebida en el firmware** (`webpage.cpp`, PROGMEM), no
  se usa ningun sistema de ficheros (LittleFS/SPIFFS): esto evita
  problemas de montaje/formateo del filesystem y simplifica subir el
  proyecto (un unico sketch, sin pasos adicionales). El unico
  inconveniente es que para editar el HTML hay que volver a compilar y
  subir el sketch completo.
- Toda la logica de negocio vive en el ESP32; la web **solo refleja**
  el estado y **envia ordenes de configuracion** (programa horario,
  temperatura objetivo, modo, redes WiFi). No hay ninguna simulacion en
  el HTML: los valores mostrados vienen siempre del WebSocket.
- El bloqueo de 10s de los botones de modo mientras las valvulas giran
  (`VALVE_MOVE_MS` en `config.h`) se calcula y aplica en el propio
  ESP32 (`schedule.cpp`), no en el navegador.
- La hora se sincroniza por NTP al conectar (ajusta el huso horario en
  la llamada a `configTime()` dentro de `esp32_jacuzzi.ino`), y tambien
  puede ajustarse manualmente desde el panel de programa de la web.
