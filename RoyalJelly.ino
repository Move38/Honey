#include "Serial.h"
ServicePortSerial sp;

enum blinkRoles     {FLOWER,   WORKER,   BROOD,  QUEEN};
byte hueByRole[5] = {78,       43,       22,     200};
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
bool isLagging;
Timer lagTimer;
#define RESOURCE_FULL_LAG 1000

bool isActive = false;;//QUEEN only variable


////COMMUNICATION VARIABLES
enum signalTypes {INERT, SUPPLY, DEMAND, TRADING};
byte tradingSignals[6];
bool isTrading = false;

////DISPLAY VARIABLES
byte saturationReduction = 10;

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
    shouldEvolve = true;
  }

  //time to change role (everyone but queen)?
  if (isFull && shouldEvolve) {
    switch (blinkRole) {
      case FLOWER:
        blinkRole = WORKER;
        break;
      case WORKER:
        blinkRole = BROOD;
        break;
      case BROOD:
        blinkRole = QUEEN;
        break;
      case QUEEN://only occurs under special circumstances, check queenLoop for logic
        blinkRole = FLOWER;
        break;
    }
    //reset all the resources and stuff
    resourceCollected = 0;
    isFull = false;
    shouldEvolve = false;
    isActive = false;
    FOREACH_FACE(f) {
      tradingSignals[f] = INERT;
    }
  }

  //if we're full, are we done lagging?
  if (lagTimer.isExpired()) {
    isLagging = false;
  }

  //run loops
  switch (blinkRole) {
    case FLOWER:
      flowerLoop();
      hiveDisplay();
      break;
    case WORKER:
      workerLoop();
      hiveDisplay();
      break;
    case BROOD:
      broodLoop();
      hiveDisplay();
      break;
    case QUEEN:
      queenLoop();
      queenDisplay();
      break;
  }

  isTrading = false;
  //make sure isTrading is accurate
  FOREACH_FACE(f) {
    if (tradingSignals[f] == TRADING) {
      isTrading = true;
    }
  }



  //set up communication
  FOREACH_FACE(f) {
    byte sendData = (blinkRole << 4) + (tradingSignals[f] << 2);
    setValueSentOnFace(sendData, f);
  }

  //do display

}

void flowerLoop() {


  //EXPORT FUNCTIONS
  if (isFull) {//EXPORT FUNCTIONS
    genericExportLoop(WORKER, WORKER);//export to WORKER blinks
  } else {
    //should we use autoResource?
    if (isTouching(WORKER)) {
      autoResource();
    }

    //have we been clicked?
    if (buttonPressed()) {
      resourceCollected++;
      if (resourceCollected == RESOURCE_STACK * 6) {
        isFull = true;
        isLagging = true;
        lagTimer.set(RESOURCE_FULL_LAG);
        resourceCollected = RESOURCE_STACK * 6;
      }
    }
  }
}

void workerLoop() {
  if (isFull) {//EXPORT FUNCTIONS
    genericExportLoop(BROOD, WORKER);//export to BROOD blinks, or other WORKER blinks if you must
  } else {//IMPORT FUNCTIONS
    genericImportLoop(FLOWER, WORKER);//import from FLOWER blinks and other WORKER blinks
    //now that we've potentially imported, do a fullness check
    if (resourceCollected >= RESOURCE_STACK * 6) {
      isFull = true;
      isLagging = true;
      lagTimer.set(RESOURCE_FULL_LAG);
      resourceCollected = RESOURCE_STACK * 6;
    }
  }
}

void broodLoop() {
  if (isFull) {//EXPORT FUNCTIONS
    genericExportLoop(QUEEN, BROOD);//export to QUEEN blinks, or other BROOD blinks if you must
  } else {//IMPORT FUNCTIONS
    genericImportLoop(WORKER, BROOD);//import from WORKER blinks and other BROOD blinks
    //now that we've potentially imported, do a fullness check
    if (resourceCollected >= RESOURCE_STACK * 6) {
      isFull = true;
      isLagging = true;
      lagTimer.set(RESOURCE_FULL_LAG);
      resourceCollected = RESOURCE_STACK * 6;
    }
  }
}

void genericExportLoop(byte primaryExportRole, byte secondaryExportRole) {
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

void genericImportLoop(byte singleStackImportRole, byte fullResourceImportRole) {
  //so the first step is to figure out what my sides are currently signalling
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
              resourceCollected = RESOURCE_STACK * 6;
            } else if (getNeighborRole(neighborData) == singleStackImportRole) {
              resourceCollected += RESOURCE_STACK;
            }
          }
          break;
      }
    }//end found face
  }//end face loop
}

void queenLoop() {
  ///ACTIVATION LOGIC
  if (isActive) {//we are currently active. Constantly lose health
    autoDamage();
    if (resourceCollected == 0) {
      isActive = false;
    }
  } else {//we are inactive. Wait until we have full resources, then go active
    if (resourceCollected == RESOURCE_STACK * 6) {
      isActive = true;
    }
  }

  ////IMPORT FUNCTIONS
  //so the first step is to figure out what my sides are currently signalling
  FOREACH_FACE(f) {
    if (isValueReceivedOnFaceExpired(f)) {//just making sure any unoccupied faces go INERT
      tradingSignals[f] = INERT;
    } else {//ok, so this face is occupied. Let's do some work
      byte neighborData = getLastValueReceivedOnFace(f);
      switch (tradingSignals[f]) {
        case INERT:// Look for a neighbor who might cause me to go into DEMAND
          //if I have a WORKER or BROOD neighbor in SUPPLY mode, we go to DEMAND
          if (getNeighborTradingSignal(neighborData) == SUPPLY && getNeighborRole(neighborData) == BROOD) {
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
              resourceCollected += RESOURCE_STACK;
            }
          }
          //an now, assuming a trade has happened, we must cap the amount of resource we can even have
          if (resourceCollected > RESOURCE_STACK * 6) {
            resourceCollected = RESOURCE_STACK * 6;
          }
          break;
      }
    }//end found face
  }//end face loop

  ////EVOLVE FUNCTION
  if (shouldEvolve) {
    isFull = true;//this allows evolution to happen next frame
  }
}

void autoResource() {
  if (resourceTimer.isExpired()) {
    //tick the resource if needed
    if (resourceCollected < RESOURCE_STACK * 6) {
      isFull = false;
      resourceCollected++;
    }
    //mark as full if needed
    if (resourceCollected >= RESOURCE_STACK * 6) {
      isFull = true;
    }
    //reset the timer
    resourceTimer.set(RESOURCE_TICK_INTERVAL);
  }
}

void autoDamage() {
  if (resourceTimer.isExpired()) {
    //tick the resource
    resourceCollected--;

    //mark as inactive if needed
    if (resourceCollected == 0) {
      isActive = false;
    }
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
  return (data >> 4);//first two bits
}

byte getNeighborTradingSignal(byte data) {
  return ((data >> 2) & 3);//second and third bit
}

///////////
//DISPLAY//
///////////

void hiveDisplay() {
  byte fullFaces = resourceCollected / RESOURCE_STACK;//returns 0-6
  byte displayHue = hueByRole[blinkRole];
  byte displaySaturation = 0;

  FOREACH_FACE(f) {
    if (f < fullFaces) {//this face is definitely full
      displaySaturation = 255 - (RESOURCE_STACK * saturationReduction);
    } else if (f == fullFaces) {//this is the one being worked on now
      displaySaturation = 255 - ((resourceCollected % RESOURCE_STACK) * saturationReduction);
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
        setColorOnFace(RED, f);
        break;
      case TRADING:
        setColorOnFace(WHITE, f);
        break;
    }
  }
}

void queenDisplay() {
  byte fullFaces = resourceCollected / RESOURCE_STACK;//returns 0-6
  byte displayHue = hueByRole[blinkRole];
  byte displayBrightness = 0;

  if (isActive) {
    FOREACH_FACE(f) {
      if (f < fullFaces) {//this face is definitely full
        displayBrightness = 255;
      } else if (f == fullFaces) {//this is the one being worked on now
        displayBrightness = 255 - ((resourceCollected % RESOURCE_STACK) * saturationReduction);
      } else {//this is empty
        displayBrightness = 255 - (RESOURCE_STACK * saturationReduction);
      }

      setColorOnFace(makeColorHSB(displayHue, 255, displayBrightness), f);
    }
  } else {
    FOREACH_FACE(f) {
      if (f < fullFaces) {//this face is definitely full
        displayBrightness = 25 + (RESOURCE_STACK * saturationReduction);
      } else if (f == fullFaces) {//this is the one being worked on now
        displayBrightness = 25 + ((resourceCollected % RESOURCE_STACK) * saturationReduction);
      } else {//this is empty
        displayBrightness = 25;
      }

      setColorOnFace(makeColorHSB(displayHue, 255, displayBrightness), f);
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
