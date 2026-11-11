[Click here](../README.md) to view the README.

## Using the code example


### Create the project

The ModusToolbox&trade; tools package provides the Project Creator as both a GUI tool and a command line tool.

<details><summary><b>Use Project Creator GUI</b></summary>

1. Open the Project Creator GUI tool

   There are several ways to do this, including launching it from the dashboard or from inside the Eclipse IDE. For more details, see the [Project Creator user guide](https://www.infineon.com/assets/row/public/documents/30/44/infineon-modustoolbox-project-creator-user-guide-usermanual-en.pdf) (locally available at *{ModusToolbox&trade; install directory}/tools_{version}/project-creator/docs/project-creator.pdf*)

2. On the **Choose Board Support Package (BSP)** page, select a kit supported by this code example. See [Supported kits](../README.md#supported-kits-make-variable-target)

   > **Note:** To use this code example for a kit not listed here, you may need to update the source files. If the kit does not have the required resources, the application may not work

3. On the **Select Application** page, use **Browse for application** to point the Project Creator at the locally delivered *psc3-provisioning-app* folder (this code example is delivered as a package rather than listed among the catalog examples). For details, see section *3.3.4 Browse for application* of the [Project Creator user guide](https://www.infineon.com/assets/row/public/documents/30/44/infineon-modustoolbox-project-creator-user-guide-usermanual-en.pdf)

   a. Select the **Target IDE**

      > **Note:** Depending on how you open the Project Creator tool, this field may be pre-selected for you

   b. (Optional) Change the suggested **New Application Name** and **New BSP Name**

   c. Click **Create** to complete the application creation process

</details>


<details><summary><b>Use Project Creator CLI</b></summary>

The 'project-creator-cli' tool can be used to create applications from a CLI terminal or from within batch files or shell scripts. This tool is available in the *{ModusToolbox&trade; install directory}/tools_{version}/project-creator/* directory.

Use a CLI terminal to invoke the 'project-creator-cli' tool. On Windows, use the command-line 'modus-shell' program provided in the ModusToolbox&trade; installation instead of a standard Windows command-line application. This shell provides access to all ModusToolbox&trade; tools. You can access it by typing "modus-shell" in the search box in the Windows menu. In Linux and macOS, you can use any terminal application.

The following example creates the provisioning application from the locally delivered *psc3-provisioning-app* template with the desired name "ProvisioningApp" configured for the *KIT_PSC3M5_EVK* BSP into the specified working directory, *C:/mtb_projects*:

   ```
   project-creator-cli --board-id KIT_PSC3M5_EVK --app-path <path-to>/psc3-provisioning-app --user-app-name ProvisioningApp --target-dir "C:/mtb_projects"
   ```

> **Note:** To target the PSOC&trade; Control C3M6 kit instead, pass `--board-id KIT_PSC3M6_EVAL`; for the PSOC&trade; Control C3M8 kit, pass `--board-id KIT_PSC3M8_EVK`.

The 'project-creator-cli' tool has the following arguments:

Argument | Description | Required/optional
---------|-------------|-----------
`--board-id` | Defined in the <id> field of the [BSP](https://github.com/Infineon?q=bsp-manifest&type=&language=&sort=) manifest | Required
`--app-path`   | Path of the locally delivered *psc3-provisioning-app* template to copy | Required
`--target-dir`| Specify the directory in which the application is to be created if you prefer not to use the default current working directory | Optional
`--user-app-name`| Specify the name of the application if you prefer to have a name other than the example's default name | Optional

<br>

> **Note:** Because the application template is supplied locally via `--app-path`, the project-creator-cli tool copies it and runs `make getlibs` to import the required libraries. For details, see the "Project creator tools" section of the [ModusToolbox&trade; tools package user guide](https://www.infineon.com/ModusToolboxUserGuide) (locally available at {ModusToolbox&trade; install directory}/docs_{version}/mtb_user_guide.pdf).

</details>

> **Note:** This application has a two-project structure (`proj_cm33_s` and `proj_cm33_ns`). The top-level *Makefile* builds, programs, and cleans both projects together; you normally do not build the sub-projects individually.


### Open the project

After fetching the libraries, you can open the application in your preferred development environment.


<details><summary><b>Eclipse IDE</b></summary>

Import the application into the Eclipse IDE for ModusToolbox&trade;. For more details, see the [Eclipse IDE for ModusToolbox&trade; user guide](https://www.infineon.com/MTBEclipseIDEUserGuide) (locally available at *{ModusToolbox&trade; install directory}/docs_{version}/mt_ide_user_guide.pdf*).

</details>


<details><summary><b>Visual Studio (VS) Code</b></summary>

Launch VS Code manually, and then open the generated *{project-name}.code-workspace* file located in the project directory.

For more details, see the [Visual Studio Code for ModusToolbox&trade; user guide](https://www.infineon.com/MTBVSCodeUserGuide) (locally available at *{ModusToolbox&trade; install directory}/docs_{version}/mt_vscode_user_guide.pdf*).

</details>


<details><summary><b>Command line</b></summary>

If you prefer to use the CLI, open the appropriate terminal, and navigate to the project directory. On Windows, use the command-line 'modus-shell' program; on Linux and macOS, you can use any terminal application. From there, you can run various `make` commands.

For more details, see the [ModusToolbox&trade; tools package user guide](https://www.infineon.com/ModusToolboxUserGuide) (locally available at *{ModusToolbox&trade; install directory}/docs_{version}/mtb_user_guide.pdf*).

</details>


### Build and program onto the target

You can build the application using the following methods:

<details><summary><b>Using Eclipse IDE</b></summary>

   1. Select the application project in the Project Explorer

   2. In the **Quick Panel**, scroll down, and click **Build Application**
</details>

<details><summary><b>Using CLI</b></summary>

   From the terminal, execute the `make build` command to build both projects using the default toolchain to the default target. The default toolchain is specified in the application's Makefile but you can override this value manually:
   ```
   make build TOOLCHAIN=<toolchain>
   ```

   Example:
   ```
   make build TOOLCHAIN=GCC_ARM
   ```
</details>

> **Note (signature suite):** By default the firmware builds the ECDSA P-256 suite. To select which provisioning signature suites are compiled in, pass `PROV_SIG_SUITES` — `ecdsa-p256`, `ecdsa-p384`, `ecdsa-p521`, `ml-dsa-87` (post-quantum, **PSC3M6/PSC3M8 only**), in any combination, for example `make build TOOLCHAIN=GCC_ARM TARGET=KIT_PSC3M6_EVAL PROV_SIG_SUITES="ecdsa-p256 ml-dsa-87"`. Selecting `ml-dsa-87` on an M5 target is a build error. See [Build-time configuration](design_and_implementation.md#build-time-configuration).

Connect the board to your PC using the provided USB cable (through the KitProg3 USB connector on M5, or the J-Link USB connector on M6) and program the board using one of the following methods:

<details><summary><b>Using Eclipse IDE</b></summary>

   1. Select the application project in the Project Explorer

   2. In the **Quick Panel**, scroll down, and click **\<Application Name> Program**
</details>


<details><summary><b>Using CLI</b></summary>

   From the terminal, execute the `make program` command to build and program both projects using the default toolchain to the default target:
   ```
   make program TOOLCHAIN=<toolchain>
   ```

   Example:
   ```
   make program TOOLCHAIN=GCC_ARM
   ```
</details>

> **Note:** `make build` only compiles and links the firmware; it does **not** flash the device. Use `make program` to write the firmware to the target.


## Debugging

You can debug the example to step through the code.


<details><summary><b>In Eclipse IDE</b></summary>

Use the **\<Application Name> Debug (KitProg3_MiniProg4)/(JLink)** configuration in the **Quick Panel**. For details, see the "Program and debug" section in the [Eclipse IDE for ModusToolbox&trade; user guide](https://www.infineon.com/MTBEclipseIDEUserGuide).

</details>


<details><summary><b>In other IDEs</b></summary>

Follow the instructions in your preferred IDE.

</details>

> **Note:** Because the application runs from secure and non-secure partitions configured by the OEM provisioning policy (on M5) or by the Device Configurator (on M6), ensure the M5 device is provisioned with the matching policy (see [Operation](../README.md#operation)) before attempting to program or debug.
