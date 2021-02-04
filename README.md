atparty2021 is a graphics demo for the 2021 @party.

BUILD INSTRUCTIONS:

    create a build folder at root directory
    cd into build
    Examples of cmake commands for different visual studios:
        cmake -G "Visual Studio 14 2015 Win64"  .. 
        cmake -G "Visual Studio 15 2017 Win64"  ..
        cmake -G "Visual Studio 16 2019" -A x64 ..
    cmake --build . --config Release
    
PACKAGING FOR DISTRIBUTION:
    This command will generate a folder that contains all the files
    necessary to run the application from the bin folder.
    
    cmake --build . --target install --config Release
    
RUN PACKAGE:
    Double click the executable in the bin folder located
    in the atparty2021-dist folder under the repo root folder.
  
    
