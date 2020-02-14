# Dictu

[![Codacy Badge](https://api.codacy.com/project/badge/Grade/ab84059049bd4ba7b7b8c1fcfaac4ea5)](https://app.codacy.com/manual/jasonhall96686/Dictu?utm_source=github.com&utm_medium=referral&utm_content=Jason2605/Dictu&utm_campaign=Badge_Grade_Dashboard)

*What is Dictu?*  
Dictu is a very simple dynamically typed programming language 
built upon the [craftinginterpreters tutorial](http://www.craftinginterpreters.com/contents.html).

*What does Dictu mean?*  
Dictu means `simplistic` in Latin.

### Dictu documentation
Documentation for Dictu can be found [here](https://jason2605.github.io/Dictu/)

## Running Dictu
```bash
$ git clone https://github.com/Jason2605/Dictu.git
$ cd Dictu
$ make dictu
$ export PATH="$PATH:`pwd`/"
$ dictu examples/guessingGame.du

## Example program
```js
def guessingGame() {
    var userInput;
    var guess = 10;

    while {
        userInput = number(input("Input your guess: "));
        print(userInput);
        if (userInput == guess) {
            print("Well done!");
            return;
        } else if (userInput < guess) {
            print("Too low!");
        } else {
            print("Too high!");
        }

        sleep(1);
    }
}

guessingGame();
```

More [here.](https://github.com/Jason2605/Dictu/tree/develop/examples)
