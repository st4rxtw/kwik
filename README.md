# kwik

Kwik is a fast, open source, cross-platform runtime & recompiler for GameMaker Studio 2.

> [!CAUTION]
> This project is still in development, things may break here and there.

## Features
* Less memory usage
* Native PSVita support (via vitaSDK)

## Supported Platforms
- Windows
- Linux
- PSVita
- 3DS (via [this](https://github.com/fr3dsp/kwik-3ds) fork despite the fact it's a bit outdated)

## Supported Backends
- OpenGL
- VitaGL (only on PSVita)

## Requirements
- CMake 3.14 or higher
- Make
- bzip2
- zlib
- ffmpeg
- A C++ compiler
- GLFW
- The data.win of the GMS2 game that you want to recompile
- PSVita:
    - vitaGL
    - libmathneon
    - vitashark
    - SceShaccCgExtSceShaccCgExt

## Development

There is a release build but I heavily recommend using the [dev](https://github.com/st4rxtw/kwik/tree/dev) branch since it implements more GMS2 functions that your project probably uses.
The best way to set up kwik is:
1. Clone the repository
2. Build kwik compiler & runtime
3. run ```cd build/compiler```
4. run ```./kwikc /path/to/data.win exportfolder```
5. cd into whatever you named your export folder and then run ```make -j$(nproc)``` to compile your game
    
## Contributions

All contributions are welcome (and very much needed)! Please open an issue or a pull request to help!
    
## License

Kwik is licensed under the GPL 3.0 license. Check the [LICENSE](LICENSE) file for more information.
    
## Credits

- [YoYo Games](https://gamemaker.io/en) - For making GameMaker Studio 2 & its [HTML5 Runtime](https://github.com/YoYoGames/GameMaker-HTML5).
- [Butterscotch & it's contributers](https://github.com/ButterscotchRunner/Butterscotch) - I initially referenced Butterscotch during the beginning of kwik's development before switching to [GameMaker's HTML5 Runtime](https://github.com/YoYoGames/GameMaker-HTML5)
