### ¿Qué es esto?
Dentro de este repositorio se encuentra la documentación tipo wiki del Reto #3 propuesto en el curso de Internet de las cosas (IoT)

## Integrantes
David esteban Diaz Vargas
Juan Pablo Moreno
Daniel Santiago Ramírez Chinchilla
## Pregunta Guía:
¿Cómo puede diseñar un sistema IoT(*) que recoja, analice y controle en tiempo real las condiciones ambientales en temperatura, humo-llama y/o gases en los cerros orientales de Bogotá para detectar incendios, notificar in situ(**) y en un “tablero de control” local y global ( ***) a las autoridades locales de manera efectiva?

(*) Utilizar el "Freenove Ultimate Starter kit" para ESP32 y el IDE de Arduino. La medición se debe ejecutar desde un ISR o hilo diferente al hilo principal de ejecución, como también, la transmisión de datos tanto en el ESP32 y como en la Raspberry Pi. Los datos capturados deben ser almacenados en una base de datos antes de su transmisión a la plataforma IoT (se recomienda https://www.sqlite.org/). También, emplear el kit de Raspberry pi y una plataforma IoT basada en nube (se recomienda Ubidots).

(**) in situ: Alarma física y visualización de variables en tiempo real.

(***) El “Tablero de control” se debe alojar en un servidor web embebido (dentro del ESP32) conectado a la WLAN ofrecida por la alcaldía y visualizado en una plataforma IoT con MQTT.
