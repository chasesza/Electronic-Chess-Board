import berserk
import serial
from datetime import datetime

UART_OFFSET = 33
USER_LICHESS_ID = 'chaseszafranski'
PGN_PATH = '/mnt/c/users/chase/dropbox/chesspgns/'
ser = serial.Serial('/dev/ttyS4')

def wait_for_game_start(stream):
    """
    waits for the game start event
    :param stream: iterator of events
    :return: the gameID
    :rtype: string
    """
    event = next(stream)
    while event["type"] != "gameStart":
        event = next(stream)
    return event["game"]["id"]


def invalid_move(stream):
    """Responds to invalid moves
    :return: nothing
    """
    print("in the invalid move function")
    ser.write(b'i')


def get_next_game_state(move_list, id, stream):
    """
    Gets the game state after the opponent's next move
    :param str move_list: all moves made so far
    :param str id: the gameID
    :param stream: iterator over game state
    :return: current game state
    :rtype: dict
    """
    state = next(stream)
    while state["type"] != "gameState":
        state = next(stream)
    if get_move_list(state) == move_list:
        if state["status"] != "started":
            return state
        if handle_draw(id):
            state = next(stream)
        else:
            state = get_next_game_state(get_move_list(state), id, stream)
    return state


def get_post_move_game_state(move_list_to_find, stream):
    """
    Gets the game state after the user's move, discard any draw offer an opponent made during the user's turn
    :param str move_list_to_find: all moves made so far
    :param str id: the gameID
    :param stream: iterator over game state
    :return: current game state
    :rtype: dict
    """
    state = next(stream)
    while state["type"] != "gameState":
        state = next(stream)
    if get_move_list(state) != move_list_to_find:
        if state["status"] != "started":
            return state
        return get_post_move_game_state(move_list_to_find, stream)
    else:
        return state


def get_move():
    """
    Gets the user's next move
    :return: the four character move
    :rtype: string
    """
    ser.timeout = 0.1
    start_byte = 'n'
    move_read = 'n'
    while start_byte != 's':
        start_byte = ser.read().decode('ascii')
    ser.write(b'a')  # acknowledge that the pc is ready to receive
    while start_byte != '':
        start_byte = ser.read().decode('ascii')
    while len(move_read) != 2:
        ser.write(b'r')  # ask for a repeat
        move_read = ser.read(2).decode('ascii')
    move_to_return = chr(((ord(move_read[0])-UART_OFFSET)%8)+ord('a'))
    move_to_return += chr(((ord(move_read[0])-UART_OFFSET)//8)+ord('1'))
    move_to_return += chr(((ord(move_read[1]) - UART_OFFSET) % 8)+ord('a'))
    move_to_return += chr(((ord(move_read[1]) - UART_OFFSET)//8)+ord('1'))
    return move_to_return


def make_move(game, next_move, list_of_moves, stream):
    """
    Makes a move and handles any invalid moves
    :param str game: ID of the game
    :param str next_move: The next move
    :param str list_of_moves: All the moves made so far
    :param stream: iterator over game state
    :return: the new move list, which can be used to avoid conflict with oddly timed draw offers
    :rtype: string
    """
    offer_draw = False
    if next_move[0] == next_move[2] and next_move[1] == next_move[3] and (next_move[1] == '1' or next_move[1] == '8'):
        client.board.resign_game(game)  # forfeit
        return list_of_moves + " ff"
    if next_move[0] == next_move[2] and next_move[1] == next_move[3] and (next_move[1] == '2' or next_move[1] == '7'):
        offer_draw = True  # offer a draw
        next_move = get_move()
    try:
        client.board.make_move(game, next_move)
    except berserk.exceptions.ResponseError:
        invalid_move(stream)
        if (game_ended):
            return "over"
        next_move = get_move()
        to_return = make_move(game, next_move, list_of_moves, stream)
        if offer_draw:
            client.board.offer_draw(game)
            get_post_move_game_state(to_return, stream)  # clears the draw offer game state
        return to_return

    if list_of_moves == "":
        to_return = next_move
    else:
        to_return = list_of_moves + " " + next_move

    if offer_draw:
        client.board.offer_draw(game)
        get_post_move_game_state(to_return, stream)  # clears the draw offer game state
    return to_return


def set_clock(state, who):
    """
    Sets the currently ticking clock
    :param dict state: the current game state
    :param string who: "black" or "white"
    :return: none
    """
    key = who[0] + "time"
    print(str(who) + ": " + str(state[key].minute) + ":" + str(state[key].second))  # this will need to be changed


def display_last_move(state):
    """
    Displays the most recent move in the game
    :param dict state: the current game state
    :return: none
    """
    prev_move = state["moves"].split()[-1]
    ser.write(b'm')
    ascii_move_int = (ord(prev_move[1])-ord('1'))*8
    ascii_move_int += ord(prev_move[0])-ord('a')
    ascii_move_int += UART_OFFSET
    ser.write(chr(ascii_move_int).encode('ascii'))
    ascii_move_int = (ord(prev_move[3]) - ord('1')) * 8
    ascii_move_int += ord(prev_move[2]) - ord('a')
    ascii_move_int += UART_OFFSET
    ser.write(chr(ascii_move_int).encode('ascii'))


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


def get_move_list(state):
    """
    Returns the move list
    :param dict state: the current game state
    :return: the move list
    :rtype: string
    """
    return state["moves"]  # this will need to be changed of course


def handle_draw(id):
    """
    Decide whether or not to accept a draw offer
    :param str id: gameID
    :return: whether the draw was accepted
    :rtype: boolean
    """
    ser.write(b'd')
    decis = get_move()  # decision
    if decis[0] == decis[2] and decis[1] == decis[3] and (decis[1] == '2' or decis[1] == '7'):
        client.board.accept_draw(id)
        return True
    else:
        client.board.decline_draw(id)
        return False


def game_over(state, stream):
    """
    Checks for the end of the game
    :param dict state: the current game state
    :param stream: iterator over game state
    :return: whether or not the game has ended
    :rtype: boolean
    """
    while state["type"] != "gameState":
        state = next(stream)
    if state["status"] == "started":
        return False
    winner = state.get("winner")
    if winner == "black":
            display_two_squares('f7c8')
    elif winner == "white":
        display_two_squares('c2f1')
    else:
        display_two_squares('e1d8')
    return True


# Get api access token
# This will need to be replaced with my actual account token
with open('./lichess personal api access token.txt') as f:
    token = f.read()

# Create session and client
session = berserk.TokenSession(token)
client = berserk.Client(session)

# Open the stream of events
event_stream = client.board.stream_incoming_events()

game_ended = False

# Wait for input to start a game
# double clicking the same button indicates that the user wants to play
play = get_move()
while play[0] == play[2] and play[1] == play[3]:

    display_two_squares('d1e8')

    # Create seek
    client.board.seek(30, 0, True)
    gameID = wait_for_game_start(event_stream)
    print("gameID:  " + gameID)
    game_stream = client.board.stream_game_state(gameID)

    game_event = next(game_stream)
    if game_event["white"]["id"] == USER_LICHESS_ID:
        color = "white"
        their_color = "black"
        display_two_squares('c2f1')
    else:
        color = "black"
        their_color = "white"
        display_two_squares('f7c8')

    print("color:  " + color)

    game_state = game_event["state"]

    move_number = 1
    move = ""
    game_ended = False
    while move != "quit":
        # user's move
        if move_number > 1 or color == "white":
            move = get_move();
            target_move_list = make_move(gameID, move, get_move_list(game_state), game_stream)
            if game_ended:
                break
            # this function is different to avoid issues with draw offers opponents placed during the user's move
            game_state = get_post_move_game_state(target_move_list, game_stream)  # state after user makes their move
            if game_over(game_state, game_stream):
                break
            set_clock(game_state, their_color)
            display_last_move(game_state)  # user move
        # opponent's move
        game_state = get_next_game_state(get_move_list(game_state), gameID, game_stream)  # state after opponent moves
        if game_over(game_state, game_stream):
            break
        set_clock(game_state, color)
        display_last_move(game_state)  # opponent's move
        print('\a')  # notification sound after opponent's move
        move_number += 1

    game_file = open(PGN_PATH+str(datetime.now())+'.txt','w')  # create game file
    game_file.write(client.games.export(gameID, True))  # write the game to the file as a pgn
    game_file.close()  # close the game file
    play = get_move()
ser.write(b'o')
input("press enter to exit")
