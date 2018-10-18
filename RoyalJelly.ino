enum blinkRoles     {SKY,   FLOWER,   WORKER,   BROOD,  QUEEN};
byte hueByRole[5] = {136,   78,       43,       22,     200};
byte blinkRole = SKY;

byte blinkNeighbors;
bool neighborLayout[6];

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:
  blinkNeighbors = 0;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      blinkNeighbors++;
      neighborLayout[f] = true;
    } else {
      neighborLayout[f] = false;
    }
  }

  //ok, so now we know what we have. let's do some work
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
    case 3:
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
    case 4:
      blinkRole = BROOD;
      //do I have an occupied space with two unoccupied neighbors?
      FOREACH_FACE(f) {
        if (neighborLayout[f]) { //ok, something here
          if (!neighborLayout[nextClockwise(f)] && !neighborLayout[nextCounterclockwise(f)]) { //both neighbors unoccupied
            blinkRole = WORKER;
          }
        }
      }
      break;
    case 5:
      blinkRole = BROOD;
      break;
    case 6:
      blinkRole = QUEEN;
      break;
  }

  hiveDisplay();
}

void hiveDisplay() {
  FOREACH_FACE(f) {
    byte saturation = 255;
    if (neighborLayout[f]) {
      saturation = 150;
    }
    Color displayColor = makeColorHSB(hueByRole[blinkRole], saturation, 255);
    setColorOnFace(displayColor, f);
  }
}

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
