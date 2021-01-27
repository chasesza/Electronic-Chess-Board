import berserk
import serial
from datetime import datetime
from threading import Thread
import signal

UART_OFFSET = 33
USER_LICHESS_ID = 'chaseszafranski'
PGN_PATH = '/mnt/c/users/chase/dropbox/chesspgns/'
ser = serial.Serial('/dev/ttyS4')
game_ended = False

def wait_for_game_start(stream):
    """
    Waits for the game start event
    :param stream: iterator of events
    :return: the game_ID
    :rtype: string
    """
    event = next(stream)
    while event["type"] != "gameStart":
        event = next(stream)
    return event["game"]["id"]


def display_two_squares(prev_move):
    """
    Turns on two leds
    :param str prev_move: the two squares to display
    :return: none
    """
    ser.write(b'm')
    ascii_move_int = (ord(prev_move[1]) - ord('1')) * 8
    ascii_move_int += ord(prev_move[0]) - ord('a')
    ascii_move_int += UART_OFFSET
    ser.write(chr(ascii_move_int).encode('ascii'))
    ascii_move_int = (ord(prev_move[3]) - ord('1')) * 8
    ascii_move_int += ord(prev_move[2]) - ord('a')
    ascii_move_int += UART_OFFSET
    ser.write(chr(ascii_move_int).encode('ascii'))


def game_over(state):
    """
    Handles the end of the game
    :param dict state: the current game state
    """
    winner = state.get("winner")
    if winner == "black":
            display_two_squares('f7c8')
    elif winner == "white":
        display_two_squares('c2f1')
    else:
        display_two_squares('e1d8')
    return 


def move_loop(game_ID):
    """
    Constantly checks for and makes moves
    :return: nothing
    """
    while not game_ended:
        handle_input(get_new_input(),game_ID)
    return


def get_new_input():
    """
    Checks for and returns new input
    :return: new, decoded input
    :rtype: string
    """
    ser.timeout = 0.2
    start_byte = ser.read().decode('ascii')
    if start_byte == 's':
        ser.write(b'a')  # acknowledge that the msp430 is sending data
        while start_byte == 's':
            start_byte = ser.read().decode('ascii')
        ascii_move = start_byte + ser.read().decode('ascii')
        while len(ascii_move) != 2:
            ser.write(b'r')  # ask for a repeat
            ascii_move = ser.read(2).decode('ascii')

        # Decode move
        move = chr(((ord(ascii_move[0])-UART_OFFSET)%8)+ord('a'))
        move += chr(((ord(ascii_move[0])-UART_OFFSET)//8)+ord('1'))
        move += chr(((ord(ascii_move[1])-UART_OFFSET)%8)+ord('a'))
        move += chr(((ord(ascii_move[1])-UART_OFFSET)//8)+ord('1'))
        return move
    
    return ''


def handle_input(move, game_ID):
    """
    Make a move, forfeit, or request a draw based on button matrix input
    :param str move: the button matrix input
    :param str game_ID: the Lichess game ID
    :return: nothing
    """

    # Check if a move was actually made
    if len(move) == 0: 
        return

    # Resign, make draw requests, and move
    if move[0] == move[2] and move[1] == move[3]:
        if move[1] == '1' or move[1] == '8':
            client.board.resign_game(game_ID)  # forfeit
        if move[1] == '2' or move[1] == '7':
            client.board.offer_draw(game_ID)
    else:
        try:
            client.board.make_move(game_ID, move)
        except berserk.exceptions.ResponseError:
            ser.write(b'i')
            print('invalid move')
    return


def handle_new_game_state(game_stream, opponent_color):
    """
    Checks for and handles changes to the game state
    :param stream: iterator over the game state
    :return: whether or not the game has ended
    :rtype: boolean
    """
    try:
        state = next(game_stream)
    except StopIteration:
        print('no iter')
        return False
    
    # This code discards chats sent by the Lichess API
    if state['type'] != 'gameState':
        return False

    # If the game is over
    if state['status'] != 'started':
        game_over(state)
        return True

    # Flash the LEDs slowly if the opponent requests a draw
    if state[opponent_color[0]+'draw'] == True:
        ser.write(b'd')

    # Display most recent move
    display_two_squares(state['moves'].split()[-1])
    
    # Print game clocks to command line
    print("White: "+str(state['wtime'].minute)+":"+str(state['wtime'].second),end='\t')
    print("Black: "+str(state['btime'].minute)+":"+str(state['btime'].second))

    return False

    
# Get api access token
with open('./lichess personal api access token.txt') as f:
    token = f.read()

# Create session and client
session = berserk.TokenSession(token)
client = berserk.Client(session)

# Open the stream of events
event_stream = client.board.stream_incoming_events()

# Wait for input to start a game
# double clicking the same button indicates that the user wants to play
play = get_new_input()
while(len(play) != 4):
    play = get_new_input()

while play[0] == play[2] and play[1] == play[3]:
    display_two_squares('d1e8')

    if play[0] == 'h' and play[1] == '1':
        # Wait for a challenge to be received, then accept it
        print('Waiting for a challenge')
        current_event = next(event_stream)

        while current_event['type'] != 'challenge':
            current_event = next(event_stream)
        challenge_ID = current_event['challenge']['id']
        client.challenges.accept(challenge_ID)

    elif play[0] == 'h' and play[1] == '2':
        # Challenge my dad to an unrated game with unlimited time
        client.challenges.create('Jeffsza',rated=False,color='random')
        print('Waiting for dad to accept')

    elif play[0] == 'h' and play[1] == '3':
        # Challenge an inputed user to an unrated game with unlimited time
        client.challenges.create(input('Who do you want to challenge?  '),rated=False,color='random')

    else:
        # Create seek
        client.board.seek(30, 0, True)
        print('Seek created')

    # Wait for the game to start, initialize game variables
    game_ID = wait_for_game_start(event_stream)
    print("game_ID:  " + game_ID)
    game_stream = client.board.stream_game_state(game_ID)

    game_event = next(game_stream)
    if game_event["white"]["id"] == USER_LICHESS_ID:
        color = "white"
        opponent_color = "black"
        display_two_squares('c2f1')
    else:
        color = "black"
        opponent_color = "white"
        display_two_squares('f7c8')

    print("color:  " + color)

    game_state = game_event["state"]

    # Main game loop
    game_ended = False
    # Make a separate thread for transmitting moves
    move_thread = Thread(target = move_loop,args = (game_ID,))
    move_thread.start()
    while True:
        if handle_new_game_state(game_stream, opponent_color):
            break
    game_ended = True
    print('The game has ended')

    # Save game as a PGN
    game_file = open(PGN_PATH+str(datetime.now())+'.txt','w')  # create game file
    game_file.write(client.games.export(game_ID, True))  # write the game to the file as a pgn
    game_file.close()  # close the game file

    # Wait for input to start a game or quit the program
    # double clicking the same button indicates that the user wants to play again
    play = get_new_input()
    while(len(play) != 4):
        play = get_new_input()
    
# Put the board into a low-power state
ser.write(b'o')
input("press enter to exit")
