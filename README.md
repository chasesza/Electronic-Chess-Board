# Electronic-Chess-Board

This electronic chess board interacts with the Lichess API to play against online opponents.

[Demonstration - YouTube Link](https://youtu.be/tFdRU_AOMV4)
<br>  

[![Demonstration - YouTube Link](https://img.youtube.com/vi/tFdRU_AOMV4/default.jpg)](https://youtu.be/tFdRU_AOMV4)

## File descriptions

overview.pdf
* pictures of the project
* explanation of the chess board user interface
* communication flow diagram
<br>  

circuit_schematic.pdf
* circuit schematic showing connections between the MSP430 launchpad, shift registers, LED matrix, and button matrix
* made in KiCad
<br>  

chess_program.py
* runs on a computer
* controls communication between Lichess's servers and the MSP430 launchpad
<br>

main.c
* runs on the MSP-EXP430FR6989 launchpad
* communicates with chess_program.py via UART
  * sends button input to chess_program.py
  * turns on LEDs as requested by chess_program.py
<br>  

script.bat
* script to run chess_program.py through the windows subsytem for linux

## Required Python modules
berserk
* Python client for the Lichess API
* can be installed by running pip install berserk
<br>  

pySerial
* communicates with the MSP430 through the virtual serial port
* can be installed by running pip install pyserial
<br>  

threading
* allows for moves to be made on a different thread than receiving game updates
<br>  

signal
* ensures that force-closing the program will work normally
<br>  

datetime
* used to name PGNs with the exact time the game finished
