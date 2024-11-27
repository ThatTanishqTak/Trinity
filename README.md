# Trinity  [![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

Trinity is a 2D-focused game engine written in C/C++. It's in very early stages of development, and I'm unsure about how far I'll take this project. Currently, most of the development has been based on TheCherno's Game Engine Series.

## Structure

The engine follows the Core-App architecture, where `Trinity` serves as the backbone powering everything as a static library. `Forge` is the level editor.

---

## Getting Started
This project requires Visual Studio 2022. As of now, Trinity only supports Windows.

### Step 1: Clone the repository
`git clone --recursive https://github.com/ThatTanishqTak/Trinity.git`


### Step 2: Set up the environment

Run `Setup.bat` in the scripts folder. It will set up `python`, `premake5`, and `VulkanSDK`. You may need to run the `Setup.bat` file again after installation.

Alternatively, if `VulkanSDK` cannot be set up through `setup.bat`, you can manually download and install it from `vulkan.lunarg.com/sdk/home` and then run the `Setup.bat` file again.

### Step 3: Build the solution

After the setup is complete, open the `Trinity.sln` file, build the solution, and navigate to the `bin/(Configuration)/Forge/` directory. Run the `.exe` file there.
