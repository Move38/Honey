#include "Serial.h"
ServicePortSerial sp;

enum blinkRoles     {SKY,   FLOWER,   WORKER,   BROOD,  QUEEN};
byte hueByRole[5] = {136,   78,       43,       22,     200};
byte blinkRole = SKY;
byte roleHold = SKY;

byte blinkNeighbors;
bool neighborLayout[6];

////RESOURCE VARIABLES
byte resourceCollected = 0;
byte resourcePip = 10;
Timer resourceTimer;
byte tickInterval = 100;
bool isFull;

////COMMUNICATION VARIABLES
enum signalTypes {INERT, SUPPLY, DEMAND, TRADING};
byte tradingSignals[6];
bool isTrading = false;

////DISPLAY VARIABLES
byte saturationReduction = 6;

/////////
//LOOPS//
/////////

void setup() {
  // put your setup code here, to run once:
  sp.begin();
}

void loop() {
  //determine neighbors
  blinkNeighbors = 0;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      blinkNeighbors++;
      neighborLayout[f] = true;
    } else {
      neighborLayout[f] = false;
    }
  }

  //determine if I'm trading (used elsewhere)
  isTrading = false;
  FOREACH_FACE(f) {
    if (tradingSignals[f] == TRADING) {
      isTrading = true;
    }
  }

  //determine role
  switch (blinkNeighbors) {
    case 0:
      blinkRole = SKY;
      break;
    case 1:
      blinkRole = FLOWER;
      break;
    case 2:
      blinkRole = WORKER;
      break;
    case 3://WORKER, unless I have contiguous neighbors
      blinkRole = WORKER;
      //do I have an occupied space with two occupied neighbors?
      FOREACH_FACE(f) {
        if (neighborLayout[f]) { //ok, something here
          if (neighborLayout[nextClockwise(f)] && neighborLayout[nextCounterclockwise(f)]) { //both neighbors occupied
            blinkRole = BROOD;
          }
        }
      }
      break;
    case 4://BROOD, unless touching a flower
      if (isTouching(FLOWER)) {
        blinkRole = WORKER;
      } else {
        blinkRole = BROOD;
      }
      break;
    case 5:
      blinkRole = BROOD;
      break;
    case 6:
      blinkRole = QUEEN;
      break;
  }

  //did we change role?
  if (blinkRole != roleHold) {//ooh, a different role
    resourceCollected = 0;
    isFull = false;
    roleHold = blinkRole;
    FOREACH_FACE(f) {
      tradingSignals[f] = INERT;
    }
  }

  //run loops
  switch (blinkRole) {
    case SKY:
      skyLoop();
      break;
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

  //set up communication
  FOREACH_FACE(f) {
    byte sendData = (blinkRole << 4) + (tradingSignals[f] << 2);
    setValueSentOnFace(sendData, f);
  }

  //do display
  hiveDisplay();


}

void skyLoop() {

}

void flowerLoop() {
  //should we use autoResource?
  if (isTouching(WORKER)) {
    autoResource();
  }

  //have we been clicked?
  if (buttonPressed()) {
    resourceCollected++;
    if (resourceCollected == resourcePip * 6) {
      isFull = true;
      resourceCollected = resourcePip * 6;
    }
  }

  //EXPORT FUNCTIONS
  if (isFull) {
    FOREACH_FACE(f) {
      if (isValueReceivedOnFaceExpired(f)) { //empty face
        tradingSignals[f] = INERT;
      } else {//an actual neighbor
        byte neighborData = getLastValueReceivedOnFace(f);
        byte tradeFace = 6;
        switch (tradingSignals[f]) {
          case INERT://look to see if my neighbor is someone I can offer my resource to
            if (getNeighborRole(neighborData) == WORKER) {
              tradingSignals[f] = SUPPLY;
            } else {//don't accidentally offer stuff to the wrong people
              tradingSignals[f] = INERT;
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
              resourceCollected = 0;
              isFull = false;
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

void workerLoop() {
  if (isFull) {//EXPORT FUNCTIONS
    FOREACH_FACE(f) {
      if (isValueReceivedOnFaceExpired(f)) { //empty face
        tradingSignals[f] = INERT;
      } else {//an actual neighbor
        byte neighborData = getLastValueReceivedOnFace(f);
        byte tradeFace = 6;
        switch (tradingSignals[f]) {
          case INERT://look to see if my neighbor is someone I can offer my resource to
            if (isTouching(BROOD)) {//if I'm touching a brood, always offer to that
              if (getNeighborRole(neighborData) == BROOD) {
                tradingSignals[f] = SUPPLY;
              } else {//don't accidentally offer stuff to the wrong people
                tradingSignals[f] = INERT;
              }
            } else {
              if (getNeighborRole(neighborData) == WORKER) {
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
              resourceCollected = 0;
              isFull = false;
            } else if (getNeighborTradingSignal(neighborData) == INERT) {//huh, some sort of interruption
              tradingSignals[f] = INERT;
              isTrading = false;
            }
            break;
        }
      }
    }
  } else {//IMPORT FUNCTIONS
    //so the first step is to figure out what my sides are currently signalling
    FOREACH_FACE(f) {
      if (isValueReceivedOnFaceExpired(f)) {//just making sure any unoccupied faces go INERT
        tradingSignals[f] = INERT;
      } else {//ok, so this face is occupied. Let's do some work
        byte neighborData = getLastValueReceivedOnFace(f);
        switch (tradingSignals[f]) {
          case INERT:// Look for a neighbor who might cause me to go into DEMAND
            //if I have a WORKER or FLOWER neighbor in SUPPLY mode, we go to DEMAND
            if (getNeighborTradingSignal(neighborData) == SUPPLY && (getNeighborRole(neighborData) == WORKER || getNeighborRole(neighborData) == FLOWER)) {
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
              if (getNeighborRole(neighborData) == WORKER) {
                resourceCollected = resourcePip * 6;
              } else if (getNeighborRole(neighborData) == FLOWER) {
                resourceCollected += resourcePip;
              }
            }
            //an now, assuming a trade has happened, we must check if we are full
            if (resourceCollected >= resourcePip * 6) {
              isFull = true;
            }
            break;
        }
      }//end found face
    }//end face loop
  }//end import functions
}

void broodLoop() {
  if (isFull) {//EXPORT FUNCTIONS
    FOREACH_FACE(f) {
      if (isValueReceivedOnFaceExpired(f)) { //empty face
        tradingSignals[f] = INERT;
      } else {//an actual neighbor
        byte neighborData = getLastValueReceivedOnFace(f);
        byte tradeFace = 6;
        switch (tradingSignals[f]) {
          case INERT://look to see if my neighbor is someone I can offer my resource to
            if (isTouching(QUEEN)) {//if I'm touching a brood, always offer to that
              if (getNeighborRole(neighborData) == QUEEN) {
                tradingSignals[f] = SUPPLY;
              } else {//don't accidentally offer stuff to the wrong people
                tradingSignals[f] = INERT;
              }
            } else {
              if (getNeighborRole(neighborData) == BROOD) {
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
              resourceCollected = 0;
              isFull = false;
            } else if (getNeighborTradingSignal(neighborData) == INERT) {//huh, some sort of interruption
              tradingSignals[f] = INERT;
              isTrading = false;
            }
            break;
        }
      }
    }
  } else {//IMPORT FUNCTIONS
    //so the first step is to figure out what my sides are currently signalling
    FOREACH_FACE(f) {
      if (isValueReceivedOnFaceExpired(f)) {//just making sure any unoccupied faces go INERT
        tradingSignals[f] = INERT;
      } else {//ok, so this face is occupied. Let's do some work
        byte neighborData = getLastValueReceivedOnFace(f);
        switch (tradingSignals[f]) {
          case INERT:// Look for a neighbor who might cause me to go into DEMAND
            //if I have a WORKER or BROOD neighbor in SUPPLY mode, we go to DEMAND
            if (getNeighborTradingSignal(neighborData) == SUPPLY && (getNeighborRole(neighborData) == WORKER || getNeighborRole(neighborData) == BROOD)) {
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
              if (getNeighborRole(neighborData) == BROOD) {
                resourceCollected = resourcePip * 6;
              } else if (getNeighborRole(neighborData) == WORKER) {
                resourceCollected += resourcePip;
              }
            }
            //an now, assuming a trade has happened, we must check if we are full
            if (resourceCollected >= resourcePip * 6) {
              isFull = true;
            }
            break;
        }
      }//end found face
    }//end face loop
  }//end import functions
}

void queenLoop() {

}

void autoResource() {
  if (resourceTimer.isExpired()) {
    //tick the resource if needed
    if (resourceCollected < resourcePip * 6) {
      isFull = false;
      resourceCollected++;
    }
    //mark as full if needed
    if (resourceCollected >= resourcePip * 6) {
      isFull = true;
    }
    //reset the timer
    resourceTimer.set(tickInterval);
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
  return (data >> 4);//first two bits
}

byte getNeighborTradingSignal(byte data) {
  return ((data >> 2) & 3);//second and third bit
}

///////////
//DISPLAY//
///////////

void hiveDisplay() {
  byte fullFaces = resourceCollected / resourcePip;//returns 0-6

  byte displayHue = hueByRole[blinkRole];
  byte displaySaturation = 0;
  FOREACH_FACE(f) {
    if (f < fullFaces) {//this face is definitely full
      displaySaturation = 255 - (resourcePip * saturationReduction);
    } else if (f == fullFaces) {//this is the one being worked on now
      displaySaturation = 255 - ((resourceCollected % resourcePip) * saturationReduction);
    } else {//this is empty
      displaySaturation = 255;
    }

    setColorOnFace(makeColorHSB(displayHue, displaySaturation, 255), f);

    //SO IMPORTANT DEBUGGING STUFF HERE
    //BRIGHTLY COLOR THE TRADE STATES
    switch (tradingSignals[f]) {
      case SUPPLY:
        setColorOnFace(BLUE, f);
        break;
      case DEMAND:
        setColorOnFace(ORANGE, f);
        break;
      case TRADING:
        setColorOnFace(RED, f);
        break;
    }
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
