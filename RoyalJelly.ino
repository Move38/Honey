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
byte tickInterval = 1500;
bool isFull;

////COMMUNICATION VARIABLES
enum signalTypes {INERT, SUPPLY, DEMAND, TRADING};
byte tradingSignals[6];

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
    //find adjacent workers and tell them you are full
    FOREACH_FACE(f) {
      if (!isValueReceivedOnFaceExpired(f)) {//a neighbor
        byte neighborData = getLastValueReceivedOnFace(f);
        if (getNeighborRole(neighborData) == WORKER) { //a worker neighbor
          tradingSignals[f] = SUPPLY;
        } else {
          tradingSignals[f] = INERT;
        }
      } else {
        tradingSignals[f] = INERT;
      }
    }

    //now, on the faces where you are asking, look for neighbors who are also asking
    byte tradeFace = 6;
    FOREACH_FACE(f) {
      if (tradingSignals[f] == DEMAND) {
        if (getNeighborTradingSignal(getLastValueReceivedOnFace(f)) == SUPPLY) {
          tradeFace = f;
        }
      }
    }

    if (tradeFace < 6) {
      tradingSignals[tradeFace] = TRADING;
    }

    //did our trading partner finally see us?
    FOREACH_FACE(f) {
      if (tradingSignals[f] == TRADING) {//this face is trying to trade
        if (getNeighborTradingSignal(getLastValueReceivedOnFace(f)) == TRADING) {//that trade partner is ready to trade
          //DO THE TRADE
          tradingSignals[f] == INERT;
          isFull = false;
          resourceCollected = 0;
        } else if (getNeighborTradingSignal(getLastValueReceivedOnFace(f)) == INERT) { //that trade partner has gone dark
          tradingSignals[f] == SUPPLY;
        }
      }
    }
  }
}

void workerLoop() {

  if (!isFull) { //IMPORT POLLEN FROM FLOWERS OR OTHER WORKERS
    //look for supplying neighbors of the WORKER or FLOWER type
    FOREACH_FACE(f) {
      if (!isValueReceivedOnFaceExpired(f)) { //a neighbor
        byte neighborData = getLastValueReceivedOnFace(f);
        if (getNeighborRole(neighborData) == WORKER && getNeighborRole(neighborData) == FLOWER) {//eligible
          if (getNeighborTradingSignal(neighborData) == SUPPLY) {//ooh, they want to supply me
            tradingSignals[f] = DEMAND;
          }
        } else {
          tradingSignals[f] = INERT;
        }
      } else {
        tradingSignals[f] = INERT;
      }
    }

    //check if any of the neighbors we have demanded from have transitioned to TRADING
    FOREACH_FACE(f) {
      if (tradingSignals[f] = DEMAND) {
        if (getNeighborTradingSignal(getLastValueReceivedOnFace(f)) == TRADING) {//hey, this person is initiating a trade
          tradingSignals[f] = TRADING;
        } else if (getNeighborTradingSignal(getLastValueReceivedOnFace(f)) == INERT) { //I guess they gave it to someone else
          tradingSignals[f] = INERT;
        }
      }
    }

    //check if any of the neighbors we are trading with have gone inert (gave us their stuff)
    FOREACH_FACE(f) {
      if (tradingSignals[f] = TRADING) {
        if (getNeighborTradingSignal(getLastValueReceivedOnFace(f)) == INERT) {
          //DO THE TRADE

          if (getNeighborRole(getLastValueReceivedOnFace(f)) == WORKER) {
            resourceCollected = resourcePip * 6;
            tradingSignals[f] = INERT;

          } else if (getNeighborRole(getLastValueReceivedOnFace(f)) == FLOWER) {
            resourceCollected += resourcePip;
            tradingSignals[f] = INERT;

          }
        }
      }
    }

    //check if these trades have caused us to become full
    if (resourceCollected >= resourcePip * 6) {
      isFull = true;
    }
    sp.print(tradingSignals[0]);
    sp.print(tradingSignals[1]);
    sp.print(tradingSignals[2]);
    sp.print(tradingSignals[3]);
    sp.print(tradingSignals[4]);
    sp.println(tradingSignals[5]);
  } else {//EXPORT POLLEN TO BROOD OR (IF WE MUST) OTHER WORKERS

  }


}

void broodLoop() {

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
