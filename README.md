# text-muncher

Text-muncher is a multithreaded program that reads in a text file and rewrites that file so that each line is numbered and shows its character count. 
Text-muncher takes as input a text file, and an integer to represent the size of an array of strings called the buffer. It then spins off four threads, each tasked with one of the following:

1) The Reader: loops through the text file and adds one line at a time to the buffer array.
2) The Measurer: awaits a new line to appear in the buffer, then measures its length and appends the character count in parentheses to the end of the line.
3) The Numberer: awaits a new line to appear in the buffer, then appends the line number to the front of the line and places it back in the buffer.
4) The Printer: awaits a line to be visited by the Reader, the Measurer, and the Numberer, then prints the lines out in order. It then clears the space in the buffer for the next line to be processed.

This project is an assignment from Adam Smith's Operating Systems course (cs475) at the University of Puget Sound.
