# DialogLoader
A loader for dialog replacements. Place dialog replacement files in `mods/DialogLoader/dialog` and this library will automatically load them into the game. With `Debug Mode` enabled, messages will be loaded from the file system every time they are requested, allowing for quick iteration on dialog files without needing to restart the game.

## Format
Dialog files are in the format in which they are converted to/from binary in the decomp project. This means anything that will cause issues or crashes in the decomp will also cause issues here. If someone wants to document the various control codes, rules and commands of this format, that would be very helpful and I'll link to that documentation here.

As a starting point, you can extract the dialog files from your ROM using this web page: https://garrettjoecox.github.io/ProxyBK_DialogLoader/ or if you have built the Banjo Kazooie decomp project, you will find them all under `assets/quiz_q` and `assets/dialog`.

## Disclaimer
The intended use here is primarily for translations, not for other mods to add their own custom dialog. Technically you can add new dialog entries by using unused text IDs, but this is not recommended as A) it can lead to conflicts between mods and B) it will require you to ship a folder of dialog files with your mod, instead of them being included in the NRM. 
