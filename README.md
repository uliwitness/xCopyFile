# xCopyFile
A HyperCard XCMD for copying a file from one place to another, optionally moving the resource fork into the data fork or restoring it so classic MacOS source code can be stored on non-Mac file systems.

# Prerequisites
To build this, you need

* CodeWarrior 10 Gold for Macintosh System 7.0
* A Mac to run it on (or an emulator like Basilisk II or SheepShaver)
* StuffIt 5 to unpack the `.sit` files
* The HyperCard stack in the `CheapVersionControl.sit` archive in this repository
  to copy the `.rsrc` files back from the data fork into the resource fork, and
  to convert the source files back from UTF8 with LF to MacRoman with CR line endings.
  (Its "Revert" button can be used to do this)

The completed app should run as far back as System 6.
