# Resultados

- `runs/`: salidas locales de pruebas; se generan automáticamente y están ignoradas por Git.
- `experimentos/configuraciones.csv`: matriz reproducible de 45 corridas y sus semillas.
- `experimentos/analisis_final/`: resultados individuales, agregados y gráficas de P2-15.

Cada corrida experimental válida procesó 800 mensajes. Los consolidados reúnen 45 corridas y 36 000 mensajes. La sesión detallada original no se versionó por su volumen; los archivos publicados permiten auditar configuraciones, conteos y estadísticas agregadas.

Los resultados de estrés y sanitizers se documentan en [`../docs/ESTRES.md`](../docs/ESTRES.md). `make clean` elimina únicamente binarios y objetos, no la evidencia de este directorio.
