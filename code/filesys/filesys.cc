// filesys.cc 
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk 
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them 
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "disk.h"
#include "bitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"
#include <stdio.h>

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known 
// sectors, so that they can be located on boot-up.
#define FreeMapSector 		0
#define DirectorySector 	1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number 
// of files that can be loaded onto the disk.
#define FreeMapFileSize 	(NumSectors / BitsInByte)
#define DirectoryFileSize 	(sizeof(DirectoryEntry) * NumDirEntries)



//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).  
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{ 
    DEBUG('f', "Initializing the file system.\n");
    if (format) {
        BitMap *freeMap = new BitMap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
    	FileHeader *mapHdr = new FileHeader;
    	FileHeader *dirHdr = new FileHeader;

        directory->Add(".", DirectorySector, TRUE);
        directory->Add("..", DirectorySector, TRUE);

            DEBUG('f', "Formatting the file system.\n");

        // First, allocate space for FileHeaders for the directory and bitmap
        // (make sure no one else grabs these!)
    	freeMap->Mark(FreeMapSector);	    
    	freeMap->Mark(DirectorySector);

        // Second, allocate space for the data blocks containing the contents
        // of the directory and bitmap files.  There better be enough space!

    	ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
    	ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

    // Flush the bitmap and directory FileHeaders back to disk
    // We need to do this before we can "Open" the file, since open
    // reads the file header off of disk (and currently the disk has garbage
    // on it!).

        DEBUG('f', "Writing headers back to disk.\n");
    	mapHdr->WriteBack(FreeMapSector);    
    	dirHdr->WriteBack(DirectorySector);

    // OK to open the bitmap and directory files now
    // The file system operations assume these two files are left open
    // while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
     
    // Once we have the files "open", we can write the initial version
    // of each file back to disk.  The directory at this point is completely
    // empty; but the bitmap has been changed to reflect the fact that
    // sectors on the disk have been allocated for the file headers and
    // to hold the file data for the directory and bitmap.
        DEBUG('f', "Writing bitmap and directory back to disk.\n");
    	freeMap->WriteBack(freeMapFile);	 // flush changes to disk
    	directory->WriteBack(directoryFile);

    	if (DebugIsEnabled('f')) {
    	    freeMap->Print();
    	    directory->Print();

            delete freeMap; 
        	delete directory; 
        	delete mapHdr; 
        	delete dirHdr;
    	}
    } else {
    // if we are not formatting the disk, just open the files representing
    // the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }

    // on demarre le systeme de fichier toujours à la racine
    currentDirectorySector = DirectorySector;
    currentDirectoryFile = new OpenFile(currentDirectorySector);

    // quand on démarre le système de fichier on a aucun fichier ouvert
    for(int i=0; i<NBFILEOPENED; i++){
        fileOpened[i] = -1;
    }

}


//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk 
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file 
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

bool
FileSystem::Create(const char *name, int initialSize)
{
    Directory *directory;
    BitMap *freeMap;
    FileHeader *hdr;
    int sector;
    bool success;

    DEBUG('f', "Creating file %s, size %d\n", name, initialSize);

    directory = new Directory(NumDirEntries);
    directory->FetchFrom(currentDirectoryFile);

    DEBUG('f', "Current directory sector %d\n", currentDirectorySector);

    if (directory->Find(name) != -1){
        printf("Error file %s already in this directory\n", name);
        success = FALSE;			// file is already in directory
        
    } else {	 
        freeMap = new BitMap(NumSectors);
        freeMap->FetchFrom(freeMapFile);
        sector = freeMap->Find();	// find a sector to hold the file header
    	if (sector == -1) 		
            success = FALSE;		// no free block for file header 

        else if (!directory->Add(name, sector, FALSE))
            success = FALSE;	// no space in directory
    	else {
        	    hdr = new FileHeader;
    	    if (!hdr->Allocate(freeMap, initialSize))
                	success = FALSE;	// no space on disk for data
    	    else {	
    	    	success = TRUE;
    		// everthing worked, flush all changes back to disk
        	    	hdr->WriteBack(sector); 		
        	    	directory->WriteBack(currentDirectoryFile);
        	    	freeMap->WriteBack(freeMapFile);
    	    }
                delete hdr;
    	}
        delete freeMap;
    }
    delete directory;
    return success;
}



bool FileSystem::CreateDirectory(const char *name){

    bool success;

    int sector;
    BitMap *freeMap;
    FileHeader *directoryFileHeader;
    Directory *newDir;
    Directory *currentDirectory;
    currentDirectory = new Directory(NumDirEntries);

    // TODO a changer avec le repertoire courant
    currentDirectory->FetchFrom(currentDirectoryFile); // on rempli le le root directory

    ASSERT(currentDirectory != NULL);

    DEBUG('f', "Creating directory: %s in the directory at the sector %d\n", name, currentDirectorySector);

    // on cherche si le nom est deja pris
    if(currentDirectory->Find(name) != -1){ // si le nom est deja pris
        printf("Error couldn't create %s directory, name already taken\n", name);
        
        success = FALSE;

    } else {

        freeMap = new BitMap(NumSectors);
        freeMap->FetchFrom(freeMapFile);
        sector = freeMap->Find();   // recherche d'un secteur sur le disque pour le dossier que l'on crée 


        if(sector == -1){ // il n'y a plus de secteur libre sur le disque;
            printf("Error couldn't create %s directory, no sector free in current directory\n", name);
            
            success = FALSE;

        } else {

            newDir = new Directory(NumDirEntries);

            DEBUG('f', "Sector for directory %s: %d\n", name, sector);

            /* On ajoute l'entrée . dans le nouveau repertoire */
            newDir->Add(".", sector, TRUE);
            /* On ajoute l'entrée du répertoire parent dans le nouveau repertoire */
            newDir->Add("..", currentDirectorySector, TRUE);
            /* On ajoute l'entrée du nouveau répertoire dans le repertoire courant */
            currentDirectory->Add(name, sector, TRUE);

            /* Création d'un FileHeader pour le nouveau repertoire */
            directoryFileHeader = new FileHeader;

            /* Alloue les secteurs néccessaires pour stocker les données de la structure de repertoire (dirEntry) */
            if(!directoryFileHeader->Allocate(freeMap, DirectoryFileSize)){
                DEBUG('f', "Error couldn't create %s directory, not enougth space in freeMap\n", name);
                success = FALSE;
            } else {

                /* 
                * Ecriture des données du nouveau répertoire sur le disque :
                * Ecriture du Fileheader du nouveau repertoire
                * lecture des entrées depuis le fileHeader et écriture des entrées.
                */
                directoryFileHeader->WriteBack(sector); 
                OpenFile *dirOpenFile = new OpenFile(sector);
                newDir->WriteBack(dirOpenFile);


                /* Met a jour les entrées du repertoire courant */
                directoryFile = new OpenFile(currentDirectorySector);
                currentDirectory->WriteBack(currentDirectoryFile);

                /* Met a jour la bitmap */
                freeMapFile = new OpenFile(FreeMapSector);
                freeMap->WriteBack(freeMapFile);


                success = TRUE;

                delete dirOpenFile;
            }

            delete newDir;
            delete directoryFileHeader;
        }

        delete freeMap;
    }

    delete currentDirectory;

    return success;


}

//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.  
//	To open a file:
//	  Find the location of the file's header, using the directory 
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile * FileSystem::Open(const char *name){
 
    int i = 0, j;


    while(i < NBFILEOPENED && fileOpened[i] != -1){ 
        i++;
    }

    if(i >= NBFILEOPENED){ // il n'y a plus de place dans le tableau des fichiers ouverts
        printf("Error couldn't open file %s, too much opened file\n", name);
        return NULL;
    } else {

        Directory *directory = new Directory(NumDirEntries);
        OpenFile *openFile = NULL;
        int sector;

        DEBUG('f', "Opening file %s\n", name);
        directory->FetchFrom(currentDirectoryFile);
        sector = directory->Find(name); 
        if (sector >= 0) {
            
            j = 0;
            while(j < NBFILEOPENED && fileOpened[j] != sector){ j++; }

            if(fileOpened[j] == sector){
                printf("File %s already opened\n", name);
            } else {
                openFile = new OpenFile(sector);	// name was found in directory 
                fileOpened[i] = sector;
            }
        }
        delete directory;
        return openFile;				// return NULL if not found
    }

}

void FileSystem::Close(int sector){

    int i = 0;

    while(i < NBFILEOPENED && fileOpened[i] != sector){ i++; }

    if(i >= NBFILEOPENED){
        printf("Error file not opened or not found\n");
    } else {
        fileOpened[i] = -1;
    }
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool
FileSystem::Remove(const char *name)
{ 
    Directory *directory;
    BitMap *freeMap;
    FileHeader *fileHdr;
    int sector;
    
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(currentDirectoryFile);
    sector = directory->Find(name);
    if (sector == -1) {
       delete directory;
       return FALSE;			 // file not found 
    }

    int i = 0;
    while(i < NBFILEOPENED && fileOpened[i] != sector){i++;}

    if(fileOpened[i] == sector){
        printf("Couldn't remove this file, opened in a process\n");
    }

    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    freeMap = new BitMap(NumSectors);
    freeMap->FetchFrom(freeMapFile);

    /* si on a pas réussi la suppression du fichier/repertoire */ 
    if(!directory->Remove(name)) {
        delete fileHdr;
        delete freeMap;
        delete directory;
        return FALSE;
    }

    fileHdr->Deallocate(freeMap);       // remove data blocks
    freeMap->Clear(sector);         // remove header block

    freeMap->WriteBack(freeMapFile);		// flush to disk
    directory->WriteBack(currentDirectoryFile);        // flush to disk
    delete fileHdr;
    delete directory;
    delete freeMap;
    return TRUE;
} 


void FileSystem::ChangeDirectory(const char *name){

    Directory *currentDirectory = new Directory(NumDirEntries);
    currentDirectory->FetchFrom(currentDirectoryFile);

    DEBUG('f', "Current directory sector %d", currentDirectorySector);
    /* Cherche dans le dossier courant l'entrée 'name' */
    int index = currentDirectory->FindIndex(name);

    if(index == -1){ /* si le nom n'est pas trouvé */
        printf("Error couldn't find %s\n", name);
    } else if(!currentDirectory->isDirectory(index)) { /* si le nom correspond à un fichier */
        printf("Error %s isn't a directory\n", name);
    } else { /*  si le nom correspond à un repertoire */
        currentDirectorySector = currentDirectory->getSectorFile(index);
        currentDirectoryFile = new OpenFile(currentDirectorySector);
        DEBUG('f', "Change directory to %s sucessfull\n", name);
        DEBUG('f', "After change directory, current directory sector = %d\n", currentDirectorySector);
    }
    delete currentDirectory;
}





void FileSystem::ChangeDirectoryPath(const char *name){

    Directory *currentDirectory = new Directory(NumDirEntries);
    char buff[FileNameMaxLen+1];
    OpenFile *currentOpenFile;
    int i, j, index, currentSector;
    currentDirectory->FetchFrom(currentDirectoryFile);

    currentSector = currentDirectorySector;

    i=0;

    do {
        j=0;
        while(name[i] != '\0' && name[i] != '/'){
            buff[j] = name[i];
            i++;
            j++;
        }

        buff[j] = '\0';

        if(name[i] == '/'){
            i++;
        }

        index = currentDirectory->FindIndex(buff);

        if(index == -1){ /* si le nom n'est pas trouvé */
            printf("Error couldn't find %s\n", buff);
            return;
        } else if(!currentDirectory->isDirectory(index)) { /* si le nom correspond à un fichier */
            printf("Error %s isn't a directory\n", buff);
            return;
        } else { /*  si le nom correspond à un repertoire */
            currentSector = currentDirectory->getSectorFile(index);
            currentOpenFile = new OpenFile(currentSector);
            currentDirectory->FetchFrom(currentOpenFile);
            DEBUG('f', "After change directory, current directory sector = %d\n", currentDirectorySector);
            delete currentOpenFile;
        }



    }  while(name[i] != '\0');


    /* met à jour les infos du dossier courant */
    currentDirectorySector = currentSector;
    currentDirectoryFile = new OpenFile(currentDirectorySector);


    delete currentDirectory;
}




//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void
FileSystem::List()
{
    Directory *directory = new Directory(NumDirEntries);

    directory->FetchFrom(directoryFile);
    directory->List();
    delete directory;
}


void FileSystem::ListCurrentDirectory(){
    Directory *directory = new Directory(NumDirEntries);

    directory->FetchFrom(currentDirectoryFile);
    directory->List();
    delete directory;
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void
FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    BitMap *freeMap = new BitMap(NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->FetchFrom(freeMapFile);
    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
} 


void FileSystem::ListOpenedFiles(){
    int i;
    for(i=0; i<NBFILEOPENED; i++){
        printf("fileOpened[%d] = %d\n", i, fileOpened[i]);
    }
}