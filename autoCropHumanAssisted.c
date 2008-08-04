/*
Copyright(c)2008 Internet Archive. Software license GPL version 2.

build leptonica first:
cd leptonlib-1.56/
./configure
make
cd src/
make
cd ../prog/
make

compile with:
g++ -ansi -Werror -D_BSD_SOURCE -DANSI -fPIC -O3  -Ileptonlib-1.56/src -I/usr/X11R6/include  -DL_LITTLE_ENDIAN -o autoCropHumanAssisted autoCropHumanAssisted.c leptonlib-1.56/lib/nodebug/liblept.a -ltiff -ljpeg -lpng -lz -lm

to calculate cropbox:
autoCropHumanAssisted filein.jpg rotateDirection cropX cropY cropW cropH bindingGap
to calculate bindingGap:
autoCropHumanAssisted filein.jpg rotateDirection cropX cropY cropW cropH
*/

#include <stdio.h>
#include <stdlib.h>
#include "allheaders.h"
#include <assert.h>
#include <math.h>   //for sqrt
#include <float.h>  //for DBL_MAX
#include <limits.h> //for INT_MAX
#include "autoCropCommon.h"

#define debugstr printf
//#define debugstr


static const l_float32  deg2rad            = 3.1415926535 / 180.;


static inline l_int32 min (l_int32 a, l_int32 b) {
    return b + ((a-b) & (a-b)>>31);
}

static inline l_int32 max (l_int32 a, l_int32 b) {
    return a - ((a-b) & (a-b)>>31);
}


/// FindBindingGap()
///____________________________________________________________________________
void FindBindingGap(PIX       *pixg,
                    l_int32   rotDir,
                    l_float32 angle, 
                    l_int32   cropX,
                    l_int32   cropY,
                    l_int32   cropW, 
                    l_int32   cropH)
{               
    ///first, rotate image by angle
    PIX *pixt = pixRotate(pixg,
                    deg2rad*angle,
                    L_ROTATE_AREA_MAP,
                    L_BRING_IN_BLACK,0,0);

    l_uint32 w = pixGetWidth( pixg );
    l_uint32 h = pixGetHeight( pixg );
    l_uint32 limitLeft = calcLimitLeft(w,h,angle);
    l_uint32 width10 = (l_uint32)(w * 0.10);

    l_uint32 left, right;
    if (1 == rotDir) {
        left  = limitLeft;
        right = width10;
    } else {
        left  = w - width10;
        right = w - limitLeft-1;
    }

    l_uint32   jTop = cropY;
    l_uint32   jBot = cropY + cropH;
    l_int32    bindingEdge;// = -1;
    l_uint32   bindingEdgeDiff;// = 0;
    float      bindingDelta = 0.0;
    CalculateSADcol(pixg, left, right, jTop, cropY+cropH, &bindingEdge, &bindingEdgeDiff);
    printf("strongest edge of gutter is at i=%d with diff=%d\n", bindingEdge, bindingEdgeDiff);
    
    double bindingLumaA = CalculateAvgCol(pixt, bindingEdge, jTop, jBot);
    printf("lumaA = %f\n", bindingLumaA);

    double bindingLumaB = CalculateAvgCol(pixt, bindingEdge+1, jTop, jBot);
    printf("lumaB = %f\n", bindingLumaB);

    double threshold = (l_uint32)((bindingLumaA + bindingLumaB) / 2);
    //TODO: ensure this threshold is reasonable
    printf("thesh = %f\n", threshold);
    
    //*thesh = (l_uint32)threshold;

    l_int32 width3p = (l_int32)(w * 0.03);
    l_int32 rightEdge, leftEdge;
    l_uint32 numBlackLines = 0;
    
    if (bindingLumaA > bindingLumaB) { //found left edge
        l_int32 i;
        l_int32 rightLimit = bindingEdge+width3p;
        rightEdge = bindingEdge; //init this something, in case we never break;
        leftEdge  = bindingEdge;
        for (i=bindingEdge+1; i<rightLimit; i++) {
            double lumaAvg = CalculateAvgCol(pixt, i, jTop, jBot);
            printf("i=%d, avg=%f\n", i, lumaAvg);
            if (lumaAvg<threshold) {
                numBlackLines++;
            } else {
                rightEdge = i-1;
                break;
            }
        }
        
        
        printf("numBlackLines = %d\n", numBlackLines);
    
    } else if (bindingLumaA < bindingLumaB) { //found right edge
        l_int32 i;
        l_int32 leftLimit = bindingEdge-width3p;
        rightEdge = bindingEdge;
        leftEdge  = bindingEdge; //init this something, in case we never break;
        
        if (leftLimit<0) leftLimit = 0;
        printf("found right edge of gutter, leftLimit=%d, rightLimit=%d\n", leftLimit, bindingEdge-1);
        for (i=bindingEdge-1; i>leftLimit; i--) {
            double lumaAvg = CalculateAvgCol(pixt, i, jTop, jBot);
            printf("i=%d, avg=%f\n", i, lumaAvg);
            if (lumaAvg<threshold) {
                numBlackLines++;
            } else {
                leftEdge = i-1;
                break;
            }
        }
        printf("numBlackLines = %d\n", numBlackLines);
    
    } else {
        //return -1; //TODO: handle error
    }

    if (1 == rotDir) {  
        printf("bindingGap: %d\n", (cropX-rightEdge)*8);
    } else if (-1 == rotDir) {  
        printf("bindingGap: %d\n", (leftEdge-(cropX+cropW))*8);    
    } else {
        assert(0);
    }
    
    l_int32 darkThresh = CalculateTreshInitial(pixt);
    l_int32 topEdge    = RemoveBackgroundTop(pixt, rotDir, darkThresh);
    l_int32 bottomEdge = RemoveBackgroundBottom(pixt, rotDir, darkThresh);
    printf("topGap: %d\n", (cropY - topEdge) * 8);
    printf("bottomGap: %d\n", (bottomEdge - (cropY+cropH)) * 8);
    
    pixDestroy(&pixt);
}


/// SkewAndCrop()
///____________________________________________________________________________
void SkewAndCrop(PIX       *pixg,
                 l_int32   rotDir,
                 l_float32 oldAngle, 
                 l_int32   oldX,
                 l_int32   oldY,
                 l_int32   oldW, 
                 l_int32   oldH,
                 l_int32   bindingGap)
{
printf("oldW = %d\n", oldW);
    l_int32 boxW10 = (l_int32)(oldW * 0.10);
    l_int32 boxH10 = (l_int32)(oldH * 0.10);
    BOX     *box   = boxCreate(oldX+boxW10, oldY+boxH10, oldW-boxW10, oldH-boxH10);
    PIX     *pixc  = pixClipRectangle(pixg, box, NULL);
    
    l_int32 darkThresh = CalculateTreshInitial(pixc);

    PIX *pixb = pixThresholdToBinary(pixc, darkThresh);
    pixWrite("/home/rkumar/public_html/outbin.png", pixb, IFF_PNG); 

    l_float32    conf, textAngle;

    printf("calling pixFindSkew\n");
    if (pixFindSkew(pixb, &textAngle, &conf)) {
        //error
        printf("angle=%.2f\nconf=%.2f\n", 0.0, -1.0);
    } else {
        printf("angle=%.2f\nconf=%.2f\n", textAngle, conf);
    }   
    
    PIX *pixSmall = pixScale(pixg, 0.125, 0.125);

    l_float32   bindingAngle;
    l_uint32    bindingThresh;
    
    l_int32 bindingEdge = FindBindingEdge2(pixSmall,
                                           rotDir,
                                           oldY/8,
                                           (oldY+oldH)/8,
                                           &bindingAngle,
                                           &bindingThresh);
    
    bindingEdge *= 8;

    #define   kSkewModeText 0
    #define   kSkewModeEdge 1
    l_int32   skewMode;
    l_float32 skewAngle;
    if (conf >= 2.0) {
        debugstr("skewMode: text\n");
        skewAngle = textAngle;
        skewMode  = kSkewModeText;
    } else {

        debugstr("skewMode: edge\n");
        //angle = (deltaT + deltaB + deltaV1 + deltaV2)/4;
        skewAngle = bindingAngle; //TODO: calculate average of four edge deltas.
        skewMode  = kSkewModeEdge;
    }

    printf("textAngle=%f\n", textAngle);
    printf("bindingAngle=%f\n", bindingAngle);

    l_int32 newX, newY, newW, newH;
    
    if (1 == rotDir) {
        newX = bindingEdge + bindingGap;
    } else if (-1 == rotDir) {
        newX = bindingEdge - bindingGap - oldW;    
    } else {
        assert(0);
    }

    l_int32 topEdge    = RemoveBackgroundTop(pixg, rotDir, darkThresh);
    l_int32 bottomEdge = RemoveBackgroundBottom(pixg, rotDir, darkThresh);
    assert( (bottomEdge - topEdge) > oldH );
    
    printf("topEdge: %d\n", topEdge);
    printf("bottomEdge: %d\n", bottomEdge);
    
    newH = oldH;
    newY = topEdge + ((bottomEdge-topEdge) - newH) / 2;
    
    newW = oldW;
    
    printf("cropX=%d\n", newX);
    printf("cropY=%d\n", newY);
    printf("cropW=%d\n", newW);
    printf("cropH=%d\n", newH);
    
}


/// main()
///____________________________________________________________________________
int main(int argc, char **argv) {
    static char  mainName[] = "autoCropHumanAssisted";

    if ((argc != 8) && (argc != 9)) {
        exit(ERROR_INT(" Syntax:  autoCropHumanAssist filein.jpg rotateDirection angle cropX cropY cropW cropH [bindingGap]",
                         mainName, 1));
    }
    
    char    *filein  = argv[1];
    l_int32   rotDir   = atoi(argv[2]);
    l_float32 angle    = atof(argv[3]); //this is usually ignored; only used in findBindingEdge mode
    l_int32   cropX    = atoi(argv[4]);
    l_int32   cropY    = atoi(argv[5]);
    l_int32   cropW    = atoi(argv[6]);
    l_int32   cropH    = atoi(argv[7]);    
    
    FILE        *fp;
    PIX         *pixs, *pixd, *pixg;
    
    if ((fp = fopenReadStream(filein)) == NULL) {
        exit(ERROR_INT("image file not found", mainName, 1));
    }
    debugstr("Opened file handle\n");

    if (8 == argc) {
        if ((pixs = pixReadStreamJpeg(fp, 0, 8, NULL, 0)) == NULL) {
           exit(ERROR_INT("pixs not made", mainName, 1));
        }
    } else {
        startTimer();
        if ((pixs = pixReadStreamJpeg(fp, 0, 1, NULL, 0)) == NULL) {
           exit(ERROR_INT("pixs not made", mainName, 1));
        }
        printf("opened large jpg in %7.3f sec\n", stopTimer());    
    }
    debugstr("Read jpeg\n");

    if (rotDir) {
        pixd = pixRotate90(pixs, rotDir);
        debugstr("Rotated 90 degrees\n");
    } else {
        pixd = pixs;
    }
    
    pixg = ConvertToGray(pixd);
    debugstr("Converted to gray\n");
    pixWrite("/home/rkumar/public_html/outgray.jpg", pixg, IFF_JFIF_JPEG); 
    
    if (8 == argc) {
        FindBindingGap(pixg, rotDir, angle, cropX/8, cropY/8, cropW/8, cropH/8);
    } else {
        l_int32 bindingGap = atoi(argv[8]);
        SkewAndCrop(pixg, rotDir, angle, cropX, cropY, cropW, cropH, bindingGap);
    }

    
    pixDestroy(&pixs);
    pixDestroy(&pixd);
    pixDestroy(&pixg);
    
    return 0;
}
