# SpectreDAW

Sintetizador y secuenciador de pasos hecho en C para la Nintendo 3DS (homebrew). Es para quien quiere hacer música en una consola portátil, con un ciclo de audio propio en tiempo real.

## Qué hace

- 3 pistas de batería (bombo, caja y hi-hat) de 16 pasos cada una.
- Pista de sintetizador con piano roll de acordes de hasta 3 notas por paso.
- Síntesis sustractiva en tiempo real: 2 osciladores (seno, sierra, cuadrada, triángulo y ruido), envolvente ADSR, filtro pasa bajas con resonancia y LFO.
- 4 espacios de patch para guardar sonidos distintos.
- BPM ajustable de 60 a 240.
- Guarda patches y patrones en la tarjeta SD con la tecla START.
- Se maneja con la pantalla táctil y los botones de la consola.

## Tecnologías

C, devkitPro con devkitARM y libctru, Makefile.

## Cómo compilarlo

Necesitas [devkitPro](https://devkitpro.org/wiki/Getting_Started) con el paquete `3ds-dev` y las variables de entorno `DEVKITPRO` y `DEVKITARM` configuradas.

```sh
make
```

Genera `SpectreDAW.3dsx` y `SpectreDAW.elf`. Para limpiar usa `make clean`.

## Cómo ejecutarlo

- En el emulador Citra: File, Load File y elige `SpectreDAW.3dsx`.
- En una consola con Luma3DS: copia el archivo a `/3ds/SpectreDAW/SpectreDAW.3dsx` en la SD y ábrelo desde el Homebrew Launcher.

Los datos se guardan en `sdmc:/3ds/SpectreDAW/patches.bin` y `pattern.bin`.

## Controles

| Vista | Control | Acción |
| --- | --- | --- |
| Secuenciador | Tocar celda de batería | Activar o apagar el paso |
| Secuenciador | Tocar celda de sintetizador | Abrir selector de notas |
| Secuenciador | A | Reproducir o detener |
| Secuenciador | Y | Editar sintetizador |
| Secuenciador | L / R | Patch anterior o siguiente |
| Secuenciador | Cruz arriba o abajo | BPM más 5 o menos 5 |
| Secuenciador | START | Guardar en SD y salir |
| Selector de notas | Tocar tecla | Agregar o quitar nota del acorde |
| Selector de notas | CLR | Limpiar el acorde del paso |
| Selector de notas | DONE o B | Confirmar y volver |
| Edición de sintetizador | L / R | Cambiar pestaña (OSC, ENV, FLT) |
| Edición de sintetizador | Tocar y arrastrar | Botones de oscilador y sliders |
| Edición de sintetizador | B | Volver al secuenciador |

## Licencia

MIT. Ver `LICENSE`.
