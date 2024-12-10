# FastFileFinder

GUI application made for personal use for very quick indexing and searching files on Windows PC.

Indexing is done by directly parsing Master File Table (MFT) and saving information about all the files in a relatively small single compressed file.  
For example for my laptop with 1.27 milion files the index file takes less than 1 second to generate and has a size of 22.6 MB. That is less than 18 Bytes on average per file to store its name, path, last modification time and size.

Searching is automatic on each key stroke and takes few miliseconds to complete.

Example screenshot from program while searching for all ".dll" files in "System32" directory sorted by size in descending order:

![fastFileFinder](https://github.com/user-attachments/assets/6592e6d2-45f4-4c74-b084-da87b5fceb45)
