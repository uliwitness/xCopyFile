// -----------------------------------------------------------------------------
//	Headers
// -----------------------------------------------------------------------------

#include <HyperXCmd.h>
#include <Types.h>
#include <Memory.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Files.h>
#include <Aliases.h>
#include <Errors.h>
#include <string.h>
#include <limits.h>
#include <Resources.h>
#include "XCmdUtils.h"
#include "MissingFiles.h"


enum FileCopyModeBit {
	kSourceRFBit = (1 << 0),
	kDestRFBit = (1 << 1)
};

enum FileCopyMode {
	kFileCopyModeDFToDF = 0,
	kFileCopyModeRFToDF = kSourceRFBit,
	kFileCopyModeRFToRF = (kSourceRFBit | kDestRFBit),
	kFileCopyModeDFToRF = kDestRFBit
};


// -----------------------------------------------------------------------------
//	Prototypes
// -----------------------------------------------------------------------------

OSErr GetVRefNumAndDirIDFromPath(CharsHandle filePath, short *vRefNum, short *dirID, unsigned char* outFilename);


// -----------------------------------------------------------------------------
//	GetVRefNumAndDirIDFromPath
// -----------------------------------------------------------------------------

OSErr GetVRefNumAndDirIDFromPath(CharsHandle filePath, short *vRefNum, short *dirID, unsigned char* outFilename) {
	OSErr		err = noErr;
	Str255		errStr = {0};
	AliasHandle	alias = NULL;
	Boolean		wasChanged = false;
	FSSpec		target = {0};
	CInfoPBRec	catInfo = {0};
	Str255		fileNameBuffer = {0};
		
	HLock(filePath);
	err = NewAliasMinimalFromFullPath(strlen(*filePath), *filePath, NULL, NULL, &alias);
	HUnlock(filePath);
	
	if (err != noErr) {
		NumToString(err, errStr);
		SetReturnValue("\pCan't locate parent folder at path: ");
		AppendReturnValue(errStr);
		return err;
	}
	
	err = ResolveAlias(NULL, alias, &target, &wasChanged);
	
	DisposeHandle((Handle)alias);
	alias = NULL;

	if (err != noErr) {
		NumToString(err, errStr);
		if (!outFilename) {
			SetReturnValue("\pError getting folder ID from path: ");
		} else {
			SetReturnValue("\pError getting file ID from path: ");
		}
		AppendReturnValue(errStr);
		return err;
	} else if (target.vRefNum == 0) { // MoreFiles says this is a bug.
		SetReturnValue("\pCouldn't find volume.");
		return nsvErr;
	}
	
	if (outFilename) {
		*vRefNum = target.vRefNum;
		*dirID = target.parID;
		BlockMove(target.name, outFilename, target.name[0] + 1);
	} else {
		memcpy(fileNameBuffer, target.name, target.name[0] + 1);	
		catInfo.dirInfo.ioVRefNum = target.vRefNum;
		catInfo.dirInfo.ioDrDirID = target.parID;
		catInfo.dirInfo.ioFDirIndex = 0;
		catInfo.dirInfo.ioNamePtr = fileNameBuffer;
		
		err = PBGetCatInfoSync(&catInfo);
		if (err != noErr) {
			NumToString(err, errStr);
			SetReturnValue("\pError getting folder ID from path: ");
			AppendReturnValue(errStr);
			return err;
		}
		
		*vRefNum = catInfo.dirInfo.ioVRefNum;
		*dirID = catInfo.dirInfo.ioDrDirID;
	}
	
	return noErr;
}



// -----------------------------------------------------------------------------
//	xcmdmain
// -----------------------------------------------------------------------------

void xcmdmain(void)
{
	Str255 				errStr = {0};
	OSErr 				err = noErr;
	short 				sourceDirID = 0;
	short 				destDirID = 0;
	short 				fRefNum = 0;
	short 				sourceVRefNum = 0;
	short 				destVRefNum = 0;
	short 				dirIndex = 0;
	CharsHandle 		sourcePath = NULL;
	CharsHandle 		destFolderPath = NULL;
	Str255 				fileCopyModeString = {0};
	enum FileCopyMode 	fileCopyMode = kFileCopyModeDFToDF;
	Str255				sourceFileName = {0};
	short				sourceFD = fnfErr;
	short				destFD = fnfErr;
	long				bytesRead = 0;
	long				bytesWritten = 0;
	Ptr					buffer = NULL;
	Str255				progressCode = {0};
	FInfo				fileInfo = {0};
	long				sourceFileLength = 0;
	long 				totalBytesWritten = 0;
	
	// Extract XCMD parameters:
	if ((sourcePath = GetIndXParameter(1)) == NULL) {
		AppendReturnValue("\pExpected source file path as first parameter.");
		return;
	}
	
	if (strcmp("?", *sourcePath) == 0) {
		AppendReturnValue("\pSyntax: xCopyFile <source file path>, <dest folder path>[, {res|data}To{res|Data}Fork]");
		return;
	} else if (strcmp("!", *sourcePath) == 0) {
		AppendReturnValue("\p(c) Copyright 2021 by Uli Kusterer, all rights reserved.");
		return;
	}
	
	if ((destFolderPath = GetIndXParameter(2)) == NULL) {
		AppendReturnValue("\pExpected destination folder path as second parameter.");
		return;
	}
	
	if (GetIndXParameter255(3, fileCopyModeString)) {
		if (EqualString(fileCopyModeString, "\presToDataFork", false, true)) {
			fileCopyMode = kFileCopyModeRFToDF;
		} else if (EqualString(fileCopyModeString, "\presToResFork", false, true)) {
			fileCopyMode = kFileCopyModeRFToRF;
		} else if (EqualString(fileCopyModeString, "\pdataToResFork", false, true)) {
			fileCopyMode = kFileCopyModeDFToRF;
		} else if (EqualString(fileCopyModeString, "\pdataToDataFork", false, true)) {
			fileCopyMode = kFileCopyModeDFToDF;
		} else if (!EqualString(fileCopyModeString, "\p", false, true)) {
			AppendReturnValue("\pUnknown file copy mode, expected {res|data}To{res|Data}Fork.");
			return;
		}
	}
	
	// Convert paths to MacOS data types:
	if (GetVRefNumAndDirIDFromPath(sourcePath, &sourceVRefNum, &sourceDirID, sourceFileName) != noErr) {
		return;
	}
	
	if (GetVRefNumAndDirIDFromPath(destFolderPath, &destVRefNum, &destDirID, NULL) != noErr) {
		return;
	}
	
	// Create destination file:
	err = HGetFInfo(sourceVRefNum, sourceDirID, sourceFileName, &fileInfo);
	if (err != noErr) {
		NumToString(err, errStr);
		SetReturnValue("\pError getting source file type: ");
		AppendReturnValue(errStr);
		return;
	}

	err = HCreate(destVRefNum, destDirID, sourceFileName, fileInfo.fdCreator, fileInfo.fdType);
	if (err != noErr) {
		NumToString(err, errStr);
		SetReturnValue("\pError creating destination file: ");
		AppendReturnValue(errStr);
		return;
	}

	if (fileCopyMode & kDestRFBit) {
		HCreateResFile(destVRefNum, destDirID, sourceFileName);
		err = ResError();
		if (err != noErr) {
			NumToString(err, errStr);
			SetReturnValue("\pError creating destination resource file: ");
			AppendReturnValue(errStr);
			return;
		}
	}
	
	// Open source and destination files:
	if (fileCopyMode & kSourceRFBit) {
		err = HOpenRF(sourceVRefNum, sourceDirID, sourceFileName, fsRdPerm, &sourceFD);
	} else {
		err = HOpen(sourceVRefNum, sourceDirID, sourceFileName, fsRdPerm, &sourceFD);
	}
	if (err != noErr) {
		NumToString(err, errStr);
		SetReturnValue("\pError opening source file: ");
		AppendReturnValue(errStr);
		return;
	}
	if (fileCopyMode & kDestRFBit) {
		err = HOpenRF(destVRefNum, destDirID, sourceFileName, fsWrPerm, &destFD);
	} else {
		err = HOpen(destVRefNum, destDirID, sourceFileName, fsWrPerm, &destFD);
	}
	if (err != noErr) {
		FSClose(sourceFD);
		NumToString(err, errStr);
		SetReturnValue("\pError creating destination file: ");
		AppendReturnValue(errStr);
		return;
	}
	
	// Make sure we start reading from the start:
	err = SetFPos(sourceFD, fsFromStart, 0);
	if (err != noErr) {
		FSClose(sourceFD);
		FSClose(destFD);
		NumToString(err, errStr);
		SetReturnValue("\pError seeking source file: ");
		AppendReturnValue(errStr);
		return;
	}
	
	err = SetFPos(destFD, fsFromStart, 0);
	if (err != noErr) {
		FSClose(sourceFD);
		FSClose(destFD);
		NumToString(err, errStr);
		SetReturnValue("\pError seeking destination file: ");
		AppendReturnValue(errStr);
		return;
	}
	
	// Determine source size for progress display:
	err = GetEOF(sourceFD, &sourceFileLength);
	if (err != noErr) {
		FSClose(sourceFD);
		FSClose(destFD);
		NumToString(err, errStr);
		SetReturnValue("\pError determining source file size: ");
		AppendReturnValue(errStr);
		return;
	}
	
	buffer = NewPtr(16 * 1024);	// 16 KB.
	
	do {
		bytesRead = GetPtrSize(buffer);
		err = FSRead(sourceFD, &bytesRead, buffer);
				
		if (err != noErr && err != eofErr) {
			NumToString(err, errStr);
			SetReturnValue("\pError reading from source file: ");
			AppendReturnValue(errStr);
			break;
		}
		if (bytesRead <= 0) {
			break;
		}
		
		bytesWritten = bytesRead;
		err = FSWrite(destFD, &bytesWritten, buffer);		
		if (err != noErr) {
			FSClose(sourceFD);
			FSClose(destFD);
			DisposePtr(buffer);
			buffer = NULL;
			NumToString(err, errStr);
			SetReturnValue("\pError writing to destination file: ");
			AppendReturnValue(errStr);
			return;
		}
		if (bytesWritten != bytesRead) {
			FSClose(sourceFD);
			FSClose(destFD);
			DisposePtr(buffer);
			buffer = NULL;
			NumToString(bytesWritten, errStr);
			SetReturnValue("\pCould only write ");
			AppendReturnValue(errStr);
			AppendReturnValue("\p bytes to destination file.");
			return;
		}

		totalBytesWritten += bytesWritten;
		
		CopyCToPString("xCopyFileProgress ", progressCode);
		NumToString(totalBytesWritten, errStr);
		AppendString(progressCode, errStr);
		AppendString(progressCode, "\p,");
		NumToString(sourceFileLength, errStr);
		AppendString(progressCode, errStr);
		SendCardMessage(gXCmdBlock, progressCode);
	} while(err != eofErr);
	
	DisposePtr(buffer);
	buffer = NULL;
	
	FSClose(sourceFD);
	FSClose(destFD);
}