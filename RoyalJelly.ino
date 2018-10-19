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

////COMMUNICATION VARIABLES
bool isFull;
bool tradingHere[6];
bool ignoreHere[6];

////DISPLAY VARIABLES
byte saturationReduction = 6;

/////////
//LOOPS//
/////////

void setup() {
  // put your setup code here, to run once:

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
    byte sendData = (blinkRole << 4) + (isFull << 3) + (tradingHere[f] << 2);
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

  //are we full? is anyone asking for our resource?
  if (isFull) {
    byte tradingFace = 7;
    FOREACH_FACE(f) {
      if (!isValueReceivedOnFaceExpired(f)) {//thar be a neighbor
        byte neighborData = getLastValueReceivedOnFace(f);
        if (getNeighborRole(neighborData) == WORKER && getIsTrading(neighborData)) {//this neighbor wants my resources
          tradingFace = f;
        }
      }
    }//end of face loop

    if (tradingFace != 7) {//so I actually found a neighbor to trade with
      isFull = false;
      resourceCollected = 0;
      tradingHere[tradingFace] = true;
    }
  }//end of full check

  //resolve trading faces that are not needed
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {//neighbor, might be trading here
      byte neighborData = getLastValueReceivedOnFace(f);
      if (!getIsTrading(neighborData) && tradingHere[f]) { //that face I'm trading on is not looking anymore
        tradingHere[f] = false;
      }
    } else {//no neighbor, definitely not trading here
      tradingHere[f] = false;
    }
  }
}

void workerLoop() {
  if (!isFull) { //IMPORT POLLEN FROM FLOWERS OR OTHER WORKERS
    byte tradeFace = 7;

    //locate neighbors who are full
    FOREACH_FACE(f) {
      if (!isValueReceivedOnFaceExpired(f)) {//a neighbor!
        byte neighborData = getLastValueReceivedOnFace(f);

        //locate full neighbors and request their resources
        if (getIsFull(neighborData)) { //a full neighbor!
          tradingHere[f] = true;
        } else {
          tradingHere[f] = false;
          ignoreHere[f] = false;//this is where we do the ignore cleanup
        }

        //see if this neighbor is actually willing to trade
        if (!ignoreHere[f]) { //a face I'm willing to look at
          if (getIsTrading(neighborData)) { //they want to trade with me!
            tradeFace = f;
          }
        }
      } else {
        tradingHere[f] = false;
        ignoreHere[f] = false;
      }//end found neighbor

    }//end face loop

    //hey, I'm trying to trade!
    if (tradeFace != 7) {
      tradingHere[tradeFace] = false;
      ignoreHere[tradeFace] = true;
      if (getNeighborRole(getLastValueReceivedOnFace(tradeFace)) == FLOWER) {
        resourceCollected += resourcePip;
      } else if (getNeighborRole(getLastValueReceivedOnFace(tradeFace)) == WORKER) {
        resourceCollected = resourcePip * 6;
      }
    }

    //check if we are full
    if (resourceCollected >= resourcePip * 6) {
      isFull = true;
    }

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

bool getIsTrading(byte data) {
  return ((data >> 2) & 1);
}

bool getIsFull(byte data) {
  return ((data >> 3) & 1);
}

byte getNeighborRole(byte data) {
  return (data >> 4);
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

    if (tradingHere[f]) {
      displaySaturation = 0;
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
