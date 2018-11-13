#include "Serial.h"
ServicePortSerial sp;

enum blinkRoles     {FLOWER,   WORKER,   BROOD,  QUEEN};
byte blinkRole = FLOWER;

byte blinkNeighbors;
bool neighborLayout[6];
bool shouldEvolve = false;

////RESOURCE VARIABLES
byte resourceCollected = 0;
#define RESOURCE_STACK 10
Timer resourceTimer;
#define RESOURCE_TICK_INTERVAL 200

bool isFull;
long fullStartTime = 0;
bool isLagging;
Timer lagTimer;
#define RESOURCE_FULL_LAG 2000
#define FULL_PULSE_INTERVAL 1000

Timer evolveTimer;
#define EVOLVE_INTERVAL 1000

bool isExporting = false;
byte exportFace = 0;
Timer exportTimer;
#define EXPORT_INTERVAL 500

////COMMUNICATION VARIABLES
enum signalTypes {INERT, SUPPLY, DEMAND, TRADING};
byte tradingSignals[6];
bool isTrading = false;

enum celebrationStates {NOMINAL, HOORAY, RESOLVING};
byte celebrationState = NOMINAL;


////DISPLAY VARIABLES
byte hueByRole[5] = {78,       43,       22,     200};
byte saturationReduction = 10;
#define BEE_SATURATION 210
#define RESOURCE_DIM 50

byte spinPosition = 0;
byte spinSteps = 7;
Timer spinTimer;
#define SPIN_INTERVAL 200
bool spinClockwise = true;

bool isCelebrating = false;
Timer celebrationTimer;
#define CELEBRATION_INTERVAL 4000

/////////
//LOOPS//
/////////

void setup() {
  // put your setup code here, to run once:
  sp.begin();
}

void loop() {
  //change role when ready?
  if (buttonLongPressed()) {
    if (shouldEvolve) {
      shouldEvolve = false;
    } else {
      shouldEvolve = true;
    }
  }

  isTrading = false;
  //make sure isTrading is accurate
  FOREACH_FACE(f) {
    if (tradingSignals[f] == TRADING) {
      isTrading = true;
    }
  }

  //run loops
  switch (blinkRole) {
    case FLOWER:
      flowerLoop();
      break;
    case WORKER:
      workerLoop();
      break;
    case BROOD:
      broodLoop();
      break;
    case QUEEN:
      queenLoop();
      break;
  }

  //DEBUG CELEBRATION
  if (buttonDoubleClicked()) {
    isCelebrating = true;
    celebrationTimer.set(CELEBRATION_INTERVAL);
    celebrationState = HOORAY;
  }

  //resolve celebration state
  if (celebrationState == NOMINAL) {
    FOREACH_FACE(f) {
      if (!isValueReceivedOnFaceExpired(f)) {//neighbor!
        if (getNeighborCelebrationState(getLastValueReceivedOnFace(f)) == HOORAY) {//hooray is being propogated
          isCelebrating = true;
          celebrationTimer.set(CELEBRATION_INTERVAL);
          celebrationState = HOORAY;
        }
      }
    }
  } else if (celebrationState == HOORAY) {
    celebrationState = RESOLVING;//default to this, then change below if we find a NOMINAL
    FOREACH_FACE(f) {
      if (!isValueReceivedOnFaceExpired(f)) {//neighbor!
        if (getNeighborCelebrationState(getLastValueReceivedOnFace(f)) == NOMINAL) {//this neighbor still needs to celebrate
          celebrationState = HOORAY;
        }
      }
    }
  } else if (celebrationState == RESOLVING) {
    celebrationState = NOMINAL;
    FOREACH_FACE(f) {
      if (!isValueReceivedOnFaceExpired(f)) {//neighbor!
        if (getNeighborCelebrationState(getLastValueReceivedOnFace(f)) == HOORAY) {//this neighbor still needs to resolve
          celebrationState = RESOLVING;
        }
      }
    }
  }

  if (celebrationTimer.isExpired()) {
    isCelebrating = false;
  }

  //set up communication
  FOREACH_FACE(f) {
    byte sendData = (blinkRole << 4) + (tradingSignals[f] << 2) + (celebrationState);
    setValueSentOnFace(sendData, f);
  }

  hiveDisplay();
}

void flowerLoop() {
  if (isFull) {
    fullLoop(WORKER, WORKER);
  } else {
    if (exportTimer.isExpired()) {
      isExporting = false;
    }

    //auto increment if I'm touching a worker
    if (isTouching(WORKER)) {
      autoResource();
    }

    //increment in large chunks if clicked
    if (buttonPressed()) {
      resourceCollected += RESOURCE_STACK;
    }

    //check fullness
    if (resourceCollected >= RESOURCE_STACK * 6) {
      isFull = true;
      fullStartTime = millis();
      isLagging = true;
      lagTimer.set(RESOURCE_FULL_LAG);
    }
  }
}

void workerLoop() {
  if (isFull) {
    fullLoop(BROOD, WORKER);
  } else {
    incompleteLoop(FLOWER, WORKER);
  }
}

void broodLoop() {
  if (isFull) {
    fullLoop(QUEEN, BROOD);
  } else {
    incompleteLoop(WORKER, BROOD);
  }
}

void queenLoop() {
  //first, deal with the tricky business of evolving
  if (isAlone()) {
    if (shouldEvolve) {
      blinkRole = FLOWER;
      resourceCollected = 0;
      isFull = false;
      isLagging = false;
      evolveTimer.set(1000);
      shouldEvolve = false;
    }
  }
  shouldEvolve = false;

  if (isFull) {
    //The first time we run this loop, we become not full immediately
    isCelebrating = true;
    celebrationTimer.set(CELEBRATION_INTERVAL);
    celebrationState = HOORAY;
    isFull = false;
    resourceCollected = 0;
  } else {
    incompleteLoop(BROOD, QUEEN);
  }
}

void fullLoop(byte primaryExportRole, byte secondaryExportRole) {
  if (shouldEvolve) {//we are in evolve mode
    blinkRole = primaryExportRole;
    resourceCollected = 0;
    isFull = false;
    isLagging = false;
    evolveTimer.set(1000);
    shouldEvolve = false;
  } else {//we are in transfer mode
    if (isLagging) {//still lagging. check if we're done
      if (lagTimer.isExpired()) {
        isLagging = false;
      }
    } else {//done lagging, DO EXPORT
      FOREACH_FACE(f) {
        if (isValueReceivedOnFaceExpired(f)) { //empty face
          tradingSignals[f] = INERT;
        } else {//an actual neighbor
          byte neighborData = getLastValueReceivedOnFace(f);
          byte tradeFace = 6;
          switch (tradingSignals[f]) {
            case INERT://look to see if my neighbor is someone I can offer my resource to
              if (isTouching(primaryExportRole)) {//if I'm touching a my primary export type, always offer to that
                if (getNeighborRole(neighborData) == primaryExportRole) {
                  tradingSignals[f] = SUPPLY;
                } else {//don't accidentally offer stuff to the wrong people
                  tradingSignals[f] = INERT;
                }
              } else {
                if (getNeighborRole(neighborData) == secondaryExportRole) {
                  tradingSignals[f] = SUPPLY;
                } else {//don't accidentally offer stuff to the wrong people
                  tradingSignals[f] = INERT;
                }
              }
              break;
            case SUPPLY://look for neighbors who have offered to take my resource. if I am trading elsewhere, don't do this
              if (!isTrading) {//no trade signals out, so we can create one
                if (getNeighborTradingSignal(neighborData) == DEMAND) {
                  tradingSignals[f] = TRADING;
                  isTrading = true;
                }
              }
              break;
            case TRADING://so now I look for my trading neighbor to go to TRADING, so I can complete the trade and go to INERT
              if (getNeighborTradingSignal(neighborData) == TRADING) {//alright, a trade is happening
                tradingSignals[f] = INERT;
                resourceCollected -= RESOURCE_STACK * 6;
                isFull = false;
                isExporting = true;
                exportTimer.set(EXPORT_INTERVAL);
                exportFace = f;
              } else if (getNeighborTradingSignal(neighborData) == INERT) {//huh, some sort of interruption
                tradingSignals[f] = INERT;
                isTrading = false;
              }
              break;
          }
        }
      }
    }
  }
}

void incompleteLoop(byte singleStackImportRole, byte fullResourceImportRole) {
  if (isExporting) {
    if (exportTimer.isExpired()) {
      isExporting = false;
    }
  } else {
    FOREACH_FACE(f) {
      if (isValueReceivedOnFaceExpired(f)) {//just making sure any unoccupied faces go INERT
        tradingSignals[f] = INERT;
      } else {//ok, so this face is occupied. Let's do some work
        byte neighborData = getLastValueReceivedOnFace(f);
        switch (tradingSignals[f]) {
          case INERT:// Look for a neighbor who might cause me to go into DEMAND
            //if I have a compatible neighbor in SUPPLY mode, we go to DEMAND
            if (getNeighborTradingSignal(neighborData) == SUPPLY && (getNeighborRole(neighborData) == singleStackImportRole || getNeighborRole(neighborData) == fullResourceImportRole)) {
              tradingSignals[f] = DEMAND;
            }
            break;
          case DEMAND:// Look for a neighbor who could send me back to INERT or into TRADING
            //if I have demanded from a neighbor, and it has reacted, I react back
            if (getNeighborTradingSignal(neighborData) == TRADING) {//ooh, a trade is offered
              tradingSignals[f] = TRADING;
            } else if (getNeighborTradingSignal(neighborData) == INERT) {//oh, they have gone inert. Bummer
              tradingSignals[f] = INERT;
            }
            break;
          case TRADING:// Look for a neighbor that will send me back to INERT and complete a trade
            //if that neighbor has gone inert, then the trade is COMPLETE
            if (getNeighborTradingSignal(neighborData) == INERT) {
              tradingSignals[f] = INERT;
              if (getNeighborRole(neighborData) == fullResourceImportRole) {
                resourceCollected += RESOURCE_STACK * 6;
              } else if (getNeighborRole(neighborData) == singleStackImportRole) {
                resourceCollected += RESOURCE_STACK;
              }
            }
            break;
        }
      }//end found face
    }//end face loop
    //now that we've potentially imported, do a fullness check
    if (resourceCollected >= RESOURCE_STACK * 6) {
      isFull = true;
      fullStartTime = millis();
      isLagging = true;
      lagTimer.set(RESOURCE_FULL_LAG);
    }
  }
}

void autoResource() {
  if (resourceTimer.isExpired()) {
    //tick the resource
    resourceCollected++;
    //reset the timer
    resourceTimer.set(RESOURCE_TICK_INTERVAL);
  }
}

bool isTouching(byte roleType) {
  bool touchCheck = false;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) { //something here
      if (getNeighborRole(getLastValueReceivedOnFace(f)) == roleType) {
        touchCheck = true;
      }
    }
  }
  return touchCheck;
}

byte getNeighborRole(byte data) {
  return (data >> 4);//1st and 2nd bits
}

byte getNeighborTradingSignal(byte data) {
  return ((data >> 2) & 3);//3rd and 4th bits
}

byte getNeighborCelebrationState(byte data) {
  return (data & 3);//5th and 6th bits
}

///////////
//DISPLAY//
///////////

void hiveDisplay() {

  byte displayHue = hueByRole[blinkRole];
  if (isFull) {
    if (isExporting) {//doing the export thing

    } else {//just kinda waiting around
      byte displaySaturation;
      long animationPosition = (millis() - fullStartTime) % FULL_PULSE_INTERVAL;//we are this far into the pulse animation
      //are we in the first half or the second half?
      if (animationPosition < FULL_PULSE_INTERVAL / 2) {//white >> color
        displaySaturation = map_m(animationPosition, 0, FULL_PULSE_INTERVAL / 2, 0, 255);
      } else {//color >> white
        displaySaturation = map_m(animationPosition - FULL_PULSE_INTERVAL / 2, 0, FULL_PULSE_INTERVAL / 2, 255, 0);
      }
      setColor(makeColorHSB(displayHue, displaySaturation, 255));
    }
  } else {
    //I could be evolving here
    if (!evolveTimer.isExpired()) {//I'm evolving. This display takes precedence!
      byte flashState = map_m(evolveTimer.getRemaining(), 0, EVOLVE_INTERVAL, 255, 0);
      byte dimState = map_m(evolveTimer.getRemaining(), 0, EVOLVE_INTERVAL, RESOURCE_DIM, 255);
      setColor(makeColorHSB(displayHue, flashState, dimState));
    } else {//not evolving, do normal display
      byte fullFaces = resourceCollected / RESOURCE_STACK;//returns 0-6
      byte displayBrightness = 0;

      FOREACH_FACE(f) {
        if (f < fullFaces) {//this face is definitely full
          displayBrightness = 255;
        } else if (f == fullFaces) {//this is the one being worked on now
          displayBrightness = map_m(resourceCollected % RESOURCE_STACK, 0, RESOURCE_STACK, RESOURCE_DIM, 255);
          //displaySaturation = 255 - ((resourceCollected % RESOURCE_STACK) * saturationReduction);
        } else {//this is empty
          displayBrightness = RESOURCE_DIM;
        }

        setColorOnFace(makeColorHSB(displayHue, 255, displayBrightness), f);
      }
    }
  }

  //now that we have displayed the state of the world, display the little bee
  if (isCelebrating) {//celebration bee spin
    if (spinTimer.isExpired()) {

      if (spinClockwise) {
        spinPosition = (spinPosition + 1) % 6;
      } else {
        spinPosition = (spinPosition + 5) % 6;
      }

      long celebrationSpinInterval = map_m(celebrationTimer.getRemaining(), CELEBRATION_INTERVAL, 0, SPIN_INTERVAL / 5, SPIN_INTERVAL);
      celebrationSpinInterval = (celebrationSpinInterval * celebrationSpinInterval) / SPIN_INTERVAL;
      spinTimer.set(celebrationSpinInterval);
    }

    Color beeColor = makeColorHSB(hueByRole[QUEEN], 255, 255);
    setColorOnFace(beeColor, spinPosition);

  } else {//normal bee spin
    if (spinTimer.isExpired()) {

      if (spinClockwise) {
        spinPosition = (spinPosition + 1) % 6;
      } else {
        spinPosition = (spinPosition + 5) % 6;
      }

      spinSteps --;

      if (spinSteps == 0) { //should I sit for a second and think about life?
        spinTimer.set(SPIN_INTERVAL * (random(2) + 2));
        spinSteps = random(11) + 9;
        //so now that I'm sitting here, should I change my direction?
        if (random(1) == 0) {
          spinClockwise = !spinClockwise;
        }
      } else {
        spinTimer.set(SPIN_INTERVAL);
      }
    }

    Color beeColor;
    if (shouldEvolve) {
      beeColor = makeColorHSB(hueByRole[blinkRole + 1], 255, 255);
    } else {
      beeColor = makeColorHSB(hueByRole[blinkRole], BEE_SATURATION, 255);
    }
    setColorOnFace(beeColor, spinPosition);
  }

}

///////////////
//CONVENIENCE//
///////////////

byte nextClockwise (byte face) {
  if (face == 5) {
    return 0;
  } else {
    return face + 1;
  }
}

byte nextCounterclockwise (byte face) {
  if (face == 0) {
    return 5;
  } else {
    return face - 1;
  }
}

long map_m(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
