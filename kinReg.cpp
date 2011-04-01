// ---- OpenGL -----
#include <GL/glut.h>
// ---- OpenCV -----
#include <cv.h>
#include <highgui.h>
// -- libfreenect --
#include "libfreenect.h"
#include "libfreenect_sync.h"
#include "libfreenect_cv.h"
// --- C++ ---
#include <stdio.h>
#include <fstream>
#include <vector>
#include <math.h>

using namespace cv;

enum Transform_Mode{ rotation, translation, full_transform, none, experimental };
Transform_Mode transform_mode = none;

// Shorten the name for easier typing
typedef pair< Vec3f, Vec3f > match;
typedef vector< match > matchList;
matchList correspondences;
match centroids( Vec3f(0,0,0), Vec3f(0,0,0) );

//lighting stuff
const GLfloat light_ambient[]  = { 0.4f, 0.4f, 0.04, 1.0f };
const GLfloat light_diffuse[]  = { 1.0f, 1.0f, 1.0f, 1.0f };
const GLfloat light_specular[] = { 1.0f, 1.0f, 1.0f, 1.0f };
const GLfloat light_position[] = { 2.0f, 5.0f, 5.0f, 0.0f };

const GLfloat mat_ambient[]    = { 0.2f, 0.2f, 1.0f, 1.0f };
const GLfloat mat_diffuse[]    = { 0.2f, 0.8f, 1.0f, 1.0f };
const GLfloat mat_specular[]   = { 1.0f, 1.0f, 1.0f, 1.0f };
const GLfloat high_shininess[] = { 100.0f };

const GLfloat mat_ambient_back[]    = { 0.5f, 0.2f, 0.2f, 1.0f };
const GLfloat mat_diffuse_back[]    = { 1.0f, 0.2f, 0.2f, 1.0f };

// Variables for Calculations and Loops
const int NUM_CAMS = 2;
int GLwindow;
float angle_between_cams = 0.f;
int mx=-1,my=-1;        // Prevous mouse coordinates
int rotangles[2] = {0}; // Panning angles
float zoom = 1;         // zoom factor
int color = 1;          // Use the RGB texture or just draw it as color
bool DRAW_SPHERES = false;

// Window Size and Position
const int window_width = 640, window_height = 480;
int window_xpos = 1000, window_ypos = 100;

// OpenGl Initialization Routines
void setupGL( int argc, char** argv );
void define_lights_and_materials();

// Callback Functions
void cbRender();
void cbTimer( int ms );
void cbReSizeGLScene( int Width, int Height);
void cbKeyPressed( unsigned char key, int x, int y);
void cbMouseMoved( int x, int y);
void cbMousePress( int button, int state, int x, int y);

// Handy functions to call in the render function
void displayCVcams();
void transformation();
void loadVertexMatrix();
void loadRGBMatrix();
void noKinectQuit();
void draw_axes();
void draw_line(Vec3b v1, Vec3b v2);

void loadBuffers( int cameraIndx, 
        unsigned int indices[window_height][window_width], 
        short xyz[window_height][window_width][3], 
        unsigned char rgb[window_height][window_width][3] );

// Might be easier to do some things if rgb and depth loaded separately
// These are here in case I want to test color/depth calibration later
void loadColor( int cameraIndx, unsigned char rgb[window_height][window_width][3] );
void loadDepth( int cameraIndx, unsigned int indices[window_height][window_width], 
        short xyz[window_height][window_width][3] );

// Computer Vision Functions
Mat joinFrames( const Mat& img1, const Mat& img2 );
matchList findFeatures( const Mat& img1, const Mat& img2 );
match calcCentroids( const matchList& corrs, const Mat& img1, const Mat& img2 );
void calcRotation( match centr, matchList corrs );
// Initial (bad) attempt at removing erroneous depth measurements
float getDepth( int cam, int x, int y );
void printMat( const Mat& A );

// Store the matrices from all cameras here
vector<Mat> rgbCV;
vector<Mat> depthCV;

// Transformation Matrices
Mat rot( 4, 4, CV_32F );
Mat trans( 4, 4, CV_32F );
Mat rottrans( 4, 4, CV_32F );

//CONSTS
const unsigned short MASK = 0x07FF;

int main( int argc, char** argv ) {

    // load the first frames (OpenCV gets upset otherwise)
    for( int cam = 0; cam < NUM_CAMS; cam++ ) {
        rgbCV.push_back( freenect_sync_get_rgb_cv(cam) );
        depthCV.push_back( freenect_sync_get_depth_cv(cam) );
    }

    setupGL( argc, argv );
    define_lights_and_materials();
    glutMainLoop();

    return 0;
}

void setupGL( int argc, char** argv ) {

    // Initialize Display Mode
    glutInit( &argc, argv );
    glutInitDisplayMode( GLUT_RGB | GLUT_DEPTH );
    glutInitDisplayMode( GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH );

    // Initialize Window
    glutInitWindowSize( window_width, window_height );
    glutInitWindowPosition( window_xpos, window_ypos );
    GLwindow = glutCreateWindow("Kinect Registration");

    // Textures and Color
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f);
    glEnable( GL_DEPTH_TEST);

    // Setup The Callbacks
    glutDisplayFunc( &cbRender );
    glutIdleFunc( &cbRender );
    glutReshapeFunc( &cbReSizeGLScene );
    cbReSizeGLScene( window_width, window_height );
    glutKeyboardFunc( &cbKeyPressed );
    glutMotionFunc( &cbMouseMoved );
    glutMouseFunc( &cbMousePress );

    // The time passed in here needs to be the same as waitKey() for OpenCV
    glutTimerFunc( 10, cbTimer, 10 );

    glScalef( .5, .5, .5 );
    //glPushMatrix();
    glClear( GL_COLOR_BUFFER_BIT );

}

void cbKeyPressed( unsigned char key, int x, int y ) {

    // Press esc to exit
    if ( key == 27 ) {
        freenect_sync_stop();
        glutDestroyWindow( GLwindow );
        exit( 0 );
    }
    else if( key == 'r' ) 
        transform_mode = rotation;
    else if( key == 't' ) 
        transform_mode = translation;
    else if( key == 'a' )
        transform_mode = full_transform;
    else if( key == 'e' )
        transform_mode = experimental;
    else if( key == 'n' )
        transform_mode = none;
    else if( key == 'f' ) {
        correspondences = findFeatures( rgbCV[0], rgbCV[1] );
        calcRotation( centroids, correspondences );
        transform_mode = full_transform;
    }
    else if ( key == 'z' )
        zoom *= 1.1f;
    else if ( key == 'x' )
        zoom /= 1.1f;
    else if ( key == 's' )
        DRAW_SPHERES == true;

}

void cbMouseMoved( int x, int y) {

    if ( mx>=0 && my>=0) {
        rotangles[0] += y-my;
        rotangles[1] += x-mx;
    }

    mx = x;
    my = y;

}

void cbMousePress( int button, int state, int x, int y) {

    if ( button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        mx = x;
        my = y;
    }

    if ( button == GLUT_LEFT_BUTTON && state == GLUT_UP) {
        mx = -1;
        my = -1;
    }
}

void cbRender() {
    float* xyz_float = new float[NUM_CAMS*window_height*window_width*3];
    short xyz[NUM_CAMS*window_height][window_width][3];
    unsigned char rgb[NUM_CAMS*window_height][window_width][3];
    unsigned int indices[NUM_CAMS*window_height][window_width];

    // Flush the OpenCV Mat's from last frame
    rgbCV.clear();
    depthCV.clear();

    // Todo: finish processing BEFORE clearing or we might 
    // process too long and see blank screen
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glLoadIdentity();

    //------------------------------

    for( int cam = 0; cam < NUM_CAMS; cam++ ) {

        loadBuffers( cam, &indices[cam*window_height], 
                &xyz[cam*window_height], &rgb[cam*window_height] ); 

    }

    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glScalef( zoom,zoom,1 );
    glTranslatef( 0,0,-3.5 );
    glRotatef( rotangles[0], 1,0,0 );
    glRotatef( rotangles[1], 0,1,0 );
    glTranslatef( 0,0,1.5 );
    glPushMatrix(); 
    draw_axes();
    glPopMatrix(); 

    if ( DRAW_SPHERES ) {
        //-------------------------------------------
        // Green sphere at first centroid
    //    glMatrixMode( GL_MODELVIEW );
        glPushMatrix();
        glColor3f(0,1,0);
        loadVertexMatrix();
        glTranslatef( centroids.first[0],centroids.first[1],centroids.first[2] );
        glBegin( GL_LINE_LOOP ); // don't workglPointSize( 0.0 );
        GLUquadricObj *quadric;
        quadric = gluNewQuadric();

        gluQuadricDrawStyle(quadric, GLU_FILL );
        gluSphere( quadric , 10 , 16 , 18 );

        gluDeleteQuadric(quadric); 
        glEndList();

        glEnd();

        glPopMatrix();

        // ---------------------------------
        // Red sphere at second centroid
        glPushMatrix();
        glColor3f(1,0,0);
        loadVertexMatrix();
        glTranslatef( centroids.second[0],centroids.second[1],centroids.second[2] );
        glBegin( GL_LINE_LOOP ); // don't workglPointSize( 0.0 );
        quadric = gluNewQuadric();

        gluQuadricDrawStyle(quadric, GLU_FILL );
        gluSphere( quadric , 10 , 16 , 18 );

        gluDeleteQuadric(quadric); 
        glEndList();

        glEnd();

        glPopMatrix();

    }

    // Now draw the depth and color buffers

    glPointSize( 1 );
#if 0
    glVertexPointer( 3, GL_SHORT, 0, xyz );

#else 
    for (int i = 0; i < NUM_CAMS*window_height; i++) {
        for (int j=0; j < window_width; j++) {
            // (i*M + j)*P + k  (N*M*P matrix)
            int indx = (i*window_width + j)*3; 
            xyz_float[indx + 0] = xyz[i][j][0] / (1.*window_width); 
            xyz_float[indx + 1] = xyz[i][j][1] / (1.*window_height); 
            xyz_float[indx + 2] = xyz[i][j][2] / (2048.); 
        }
    }

    glVertexPointer( 3, GL_FLOAT, 0, xyz_float );
#endif 
    glColorPointer( 3, GL_UNSIGNED_BYTE, 0, rgb );
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    // Still experimenting with a few things here

    glPushMatrix();
    //glScalef( 1./640., 1./480., 1./2048. );
    //loadVertexMatrix();
    glPushMatrix();
    //glRotatef(180, 0, 1, 0);
    //glRotatef(180, 0, 0, 1);
    //glRotatef(90,0,1,0);
    transformation();
    //loadVertexMatrix();
    //glRotatef(90,0,1,0);
    glDrawArrays(GL_POINTS, 0, window_width*window_height);
    glPopMatrix();
    //loadVertexMatrix();
    glDrawArrays(GL_POINTS, window_width*window_height, window_width*window_height);
    glPopMatrix();

    glDisable( GL_TEXTURE_2D );

    displayCVcams();

    delete[] xyz_float;

    glutSwapBuffers();
}

void displayCVcams() {

    Mat tmp = rgbCV[0].clone();
    for( int cam = 0; cam < NUM_CAMS; cam++ ) {
        cvtColor( rgbCV[cam], tmp, CV_RGB2BGR );
        rgbCV[cam] = tmp.clone();
    }

    Mat rgb = joinFrames( rgbCV[0], rgbCV[1] );
    imshow( "Camera 0 | Camera 1", rgb );

    // Time here needs to be the same as cbTimer
    // returns -1 if no key pressed
    char key = waitKey( 10 );

    // If someone presses a button while a cv window 
    // is in the foreground we want the behavior to
    // be the same as for the OpenGL window, so call 
    // OpenGL's cbKeyPressed callback function
    if( key != -1 ) 
        cbKeyPressed( key, 0, 0 );

}

// This ensures that OpenGL and OpenCV play nicely together
// ms needs to be the same time as OpenCV's waitKey( ms )
void cbTimer( int ms ) {

    glutTimerFunc( ms, cbTimer, ms );
    glutPostRedisplay();

}

void cbReSizeGLScene( int Width, int Height) {

    glViewport( 0,0,Width,Height );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    gluPerspective( 60, 4/3., 0.3, 200 );
    glMatrixMode( GL_MODELVIEW );
}

void noKinectQuit() {
    printf( "Error: Kinect not connected?\n" );
    exit( 1 );
}

void loadBuffers( int cameraIndx, 
        unsigned int indices[window_height][window_width], 
        short xyz[window_height][window_width][3], 
        unsigned char rgb[window_height][window_width][3] ) {

    rgbCV.push_back( freenect_sync_get_rgb_cv(cameraIndx) );
    depthCV.push_back( freenect_sync_get_depth_cv(cameraIndx) );


    if( rgbCV[cameraIndx].empty() || depthCV[cameraIndx].empty() ) 
        noKinectQuit();

    for ( int i = 0; i < window_height; i++ ) {
        for ( int j = 0; j < window_width; j++ ) {
            xyz[i][j][0] = j;
            xyz[i][j][1] = i;
            xyz[i][j][2] = depthCV[cameraIndx].at<short>( i, j ) & MASK;
            indices[i][j] = (cameraIndx * window_height + i)*window_width + j; 
#if 0
            if( (depthCV[cameraIndx].at<short>( i, j ) & MASK) !=
                    depthCV[cameraIndx].at<short>( i, j ) ) {
                printf(" depth with mask = 0x%0x\n", depthCV[cameraIndx].at<short>( i, j ) & MASK);
                printf(" depth without mask = 0x%0x\n", depthCV[cameraIndx].at<short>( i, j ) );
                printf(" depth with mask = %d\n", depthCV[cameraIndx].at<short>( i, j ) & MASK);
                printf(" depth without mask = %d\n", depthCV[cameraIndx].at<short>( i, j ) );
            }
#endif
            Vec3b color = rgbCV[cameraIndx].at<Vec3b>(i,j); 
            rgb[i][j][0] = color[0];
            rgb[i][j][1] = color[1];
            rgb[i][j][2] = color[2];
        }
    }
}

// not used here for testing
void loadColor( int cameraIndx, unsigned char rgb[window_height][window_width][3] ) {

    rgbCV.push_back( freenect_sync_get_rgb_cv(cameraIndx) );
    if( rgbCV[cameraIndx].empty() )
        noKinectQuit();

    for ( int i = 0; i < window_height; i++ ) {
        for ( int j = 0; j < window_width; j++ ) {
            Vec3b color = rgbCV[cameraIndx].at<Vec3b>(i,j); 
            rgb[i][j][0] = color[0];
            rgb[i][j][1] = color[1];
            rgb[i][j][2] = color[2];
        }
    }

}

// not used here for testing
void loadDepth( int cameraIndx, unsigned int indices[window_height][window_width], 
        short xyz[window_height][window_width][3] ) {

    depthCV.push_back( freenect_sync_get_depth_cv(cameraIndx) );
    if( depthCV[cameraIndx].empty() ) 
        noKinectQuit();

    for ( int i = 0; i < window_height; i++ ) {
        for ( int j = 0; j < window_width; j++ ) {
            xyz[i][j][0] = j;
            xyz[i][j][1] = i;
            xyz[i][j][2] = depthCV[cameraIndx].at<short>( i, j ) & MASK;
            indices[i][j] = (cameraIndx * window_height + i)*window_width + j; 
        }
    }

}

// Do the projection from u,v,depth to X,Y,Z directly in an opengl matrix
// These numbers come from a combination of the ros kinect_node wiki, and
// nicolas burrus' posts.
void loadVertexMatrix() {

    float fx = 594.21f;
    float fy = 591.04f;
    float a = -0.0030711f;
    float b = 3.3309495f;
    float cx = 339.5f;
    float cy = 242.7f;
    GLfloat mat[16] = {
        1/fx,     0,  0, 0,
        0,    -1/fy,  0, 0,
        0,       0,  0, a,
        -cx/fx, cy/fy, -1, b
    };
    glMultMatrixf( mat);
}

void transformation() {
    if( transform_mode == none ){
        GLfloat mat[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        glMultMatrixf( mat );
    }
    if( transform_mode == experimental ){
        GLfloat mat[16] = {
            0.997846, -0.065586, -0.000841,0,
            0.065586, 0.997847, -0.000093, 0,
            0.000845, 0.000038, 1.000000, 0,
            0, 0, 0, 1
        };
        glMultMatrixf( mat );
    }
    else if( transform_mode == full_transform ) {
        glMultTransposeMatrixf( (GLfloat*)rottrans.data );
    }
    else if( transform_mode == rotation ) {
        glMultMatrixf( (GLfloat*)rot.data );
    }
    else if( transform_mode == translation ) {
        glMultMatrixf( (GLfloat*)trans.data ); 
    }
}

// This matrix comes from a combination of nicolas burrus's calibration post
// and some python code I haven't documented yet.
void loadRGBMatrix() {

    float mat[16] = {
        5.34866271e+02,   3.89654806e+00,   0.00000000e+00,   1.74704200e-02,
        -4.70724694e+00,  -5.28843603e+02,   0.00000000e+00,  -1.22753400e-02,
        -3.19670762e+02,  -2.60999685e+02,   0.00000000e+00,  -9.99772000e-01,
        -6.98445586e+00,   3.31139785e+00,   0.00000000e+00,   1.09167360e-02
    };
    glMultMatrixf( mat);
}

Mat joinFrames( const Mat& img1, const Mat& img2 ) {

    Mat rslt = Mat::zeros( img1.rows, img1.cols*2, img1.type());
    for( int i = 0; i < img1.rows; i++ )
        for( int j = 0; j < img1.cols; j++ ) {
            Vec3b p = img1.at<Vec3b>( i,j);
            Vec3b q = img2.at<Vec3b>(i,j);
            rslt.at<Vec3b>(i,j) = p;
            rslt.at<Vec3b>(i,j+window_width) = q;
        }

    return rslt;
}

matchList findFeatures( const Mat& img1, const Mat& img2 ) {

    // Store all of the SIFT features and their respective correspondences here
    vector<KeyPoint> keypoints1, keypoints2;
    Mat descriptors1, descriptors2;

    printf( "threshold = %f\n", cv::SIFT::DetectorParams::GET_DEFAULT_THRESHOLD());
    printf( "Edgethreshold = %f\n", cv::SIFT::DetectorParams::GET_DEFAULT_EDGE_THRESHOLD());

    // Control the first parameter to increase or decrease the number of features found.
    // Lowering the threshold, increases the features. Raising it has the inverse effect.
    SiftFeatureDetector sift( .06, 10);

    printf( "\nFinding Features....\n\n");
    // Detect sift features and store them in the keypoints
    sift.detect( img1, keypoints1 );
    sift.detect( img2, keypoints2 );

    printf( "Features Found!\n\n");
    printf( "Each Potential match is drawn in Red. \n\n1) If it's an accurate correspondence "  
            "\n\n    press 'y' for yes \n\n    press any other key for no \n\n2) The selected corresondences "
            "will be drawn in green on the screen. \n3) After each selection, "  
            "more 'bad' correspondences will be filtered out from the remaining matches.\n");

    // Extract the descriptors from each image for comparison and matching
    SiftDescriptorExtractor extractor;
    extractor.compute( img1, keypoints1, descriptors1 );
    extractor.compute( img2, keypoints2, descriptors2 );

    // Do some matching!
    BruteForceMatcher< L2<float> > matcher;
    vector<DMatch> matches;
    matcher.match( descriptors1, descriptors2, matches );

    // Draw the matches on to the img_matches matrix
    Mat img_matches;
    drawMatches( img1, keypoints1, img2, keypoints2, matches, img_matches);

    // Put both images side by side in one image
    Mat rslt = joinFrames( img1, img2 );

    // -----------------Examine each coorespondence one at a time--------------

    // Store the cumulative slopes of correspondences here
    double totSlope;
    // This is where we'll store the correspondences for future 
    matchList corrs;

    int i;
    for( i=0; i < (int)matches.size(); i++ ) {

        Mat tmp = rslt.clone();

        // These are the actual points in the matrices
        Point2f pt1 = keypoints1[matches[i].queryIdx].pt;
        Point2f pt2 = keypoints2[matches[i].trainIdx].pt;

        // These will be the points transformed to the joined image coordinate system
        // to use in slope calculations and comparisons
        Point2f transpt1 = Point2f( pt1.x,window_height-pt1.y);
        Point2f transpt2 = Point2f( pt2.x+window_width,window_height-pt2.y);

        // Draw circles around the correspondences
        circle( tmp, pt1, 3, Scalar( 0,0,255), 1, CV_AA );
        circle( tmp, pt2+Point2f(window_width,0), 3, Scalar(0,0,255), 1, CV_AA );
        line( tmp, pt1, pt2+Point2f( (float)window_width,0.0), Scalar(0,0,255), 1, CV_AA );

        // Show this match and ask the user if it's an accurate correspondence
        imshow( "Matching Correspondences", tmp);
        char c = waitKey( 0);

        if( c == 'y' || c == 'Y' ) {
            // Store the correspondences in a vector of pairs, where the
            // members of each pair are Vec3f
            corrs.push_back( match( Vec3f(pt1.x,pt1.y,1), Vec3f(pt2.x,pt2.y,1) ) );

            // Calculate the slope of the line between these, to filter out bad future matches
            float slp = (transpt2.y-transpt1.y)/(transpt2.x-transpt1.x);
            totSlope = slp; 
            printf("-------------------MACTH ACCEPTED------------------\n");
            printf("(x1, y1) (x2, y2) = (%f,%f) (%f,%f)\n", transpt1.x, transpt1.y, transpt2.x, transpt2.y);
            printf("pt = %d, slope = %f\n",i,slp);
            break;
        }
    }

    // Continue going through the matches to find more accurate correspondences
    for( i=i+1; i < (int)matches.size(); i++ ) {

        bool broken = false;
        Mat tmp = rslt.clone();

        // These are the actual points in the matrices
        Point2f pt1 = keypoints1[matches[i].queryIdx].pt;
        Point2f pt2 = keypoints2[matches[i].trainIdx].pt;

        // These will be the points transformed to the joined image coordinate system
        // to use in slope calculations and comparisons
        Point2f transpt1 = Point2f(pt1.x,window_height-pt1.y);
        Point2f transpt2 = Point2f(pt2.x+window_width,window_height-pt2.y);
        float slp = (transpt2.y-transpt1.y)/(transpt2.x-transpt1.x);
        printf("\n-----------CORRESPONDENCE CANDIDATE--------\n"); 
        printf("(x1, y1) (x2, y2) = (%f,%f) (%f,%f)\n", transpt1.x, transpt1.y, transpt2.x, transpt2.y);
        printf( "match # = %d, slope = %f, abs( slp - average )  = %f\n", i, slp, abs(slp - totSlope/corrs.size()) );
        printf("Avgerage Slope = %f\n", totSlope/corrs.size());
        printf("Correspondences Found = %d\n", (int)corrs.size());

        // ---------------------------Filter Out Points----------------------

        // If slope is not close to average don't even look at it
        if( abs( slp - totSlope/corrs.size() ) > .05 ) {
            printf("-----------------INVALID SLOPE---------------\n");
            printf( "slope = %f, abs( slp - average )  = %f\n", slp, abs(slp - totSlope/corrs.size()) );
            continue;
        }

        // If this point is one of the current correspondences, get rid of it
        // because one point can't have two distinct correspondences
        for( int j = 0; j < (int)corrs.size(); j++ ) {

            // Draw current correspondences in green
            Point2f firstpt2D = Point2f(corrs[j].first[0], corrs[j].first[1]);
            Point2f secondpt2D = Point2f(corrs[j].second[0], corrs[j].second[1]);
            circle( tmp, firstpt2D, 3, Scalar(0,255,0), 1, CV_AA );
            circle( tmp, secondpt2D+Point2f(window_width,0), 3, Scalar(0,255,0), 1, CV_AA );
            line( tmp, firstpt2D, secondpt2D+Point2f(window_width,0), Scalar(0,255,0), 1, CV_AA );
            if( pt1 == firstpt2D || pt2 == secondpt2D ) {
                broken = true;
                break;
            }
        }
        if( broken ) {
            broken = false;
            continue;
        }

        // -------------------------------------------------------------------

        // Draw each match in question in Red so the user can see it and decide if it's accurate
        circle( tmp, pt1, 3, Scalar(0,0,255), 1, CV_AA );
        circle( tmp, pt2+Point2f(window_width,0), 3, Scalar(0,0,255), 1, CV_AA );
        line( tmp, pt1, pt2+Point2f((float)window_width,0.0), Scalar(0,0,255), 1, CV_AA );
        imshow("Matching Correspondences", tmp);
        char c = waitKey(0);
        if( c == 'y' || c == 'Y' ) {
            // Store the correspondences in a vector of pairs, where the members of each pair are points
            corrs.push_back( match( Vec3f(pt1.x,pt1.y,1), Vec3f(pt2.x,pt2.y,1) ) );
            totSlope += slp;
            printf("-------------------MACTH ACCEPTED------------------\n");
            printf("(x1, y1) (x2, y2) = (%f,%f) (%f,%f)\n", transpt1.x, transpt1.y, transpt2.x, transpt2.y);
            printf("pt = %d, slope = %f\n",i,slp);
        }
        else if( c == 'q' ) {
            break;
        }
        else printf("-------------------MATCH DENIED---------------\n");
    }

    printf("\n\nCorrespondences have been found!\n\n");

    // Show all of the matches to compare to those the user decided to be accurate correspondences
    imshow("SIFT matches", img_matches);

    // Put both images side by side in one image
    Mat centrs = joinFrames( img1, img2 );
    centroids = calcCentroids( corrs, rgbCV[0], rgbCV[1] );
    // Draw green circles around the correspondences in each image
    for( int i = 0; i < (int)corrs.size(); i++ ) {
        circle( centrs, Point2f( corrs[i].first[0], corrs[i].first[1] ), 3, Scalar(0,255,0), 1, CV_AA );
        circle( centrs, Point2f( corrs[i].second[0], corrs[i].second[1] )
                +Point2f(window_width,0), 3, Scalar(0,255,0), 1, CV_AA );
    }

    // Draw red circles around the centroids
    circle( centrs, Point2f(centroids.first[0],centroids.first[1]), 3, Scalar(0,0,255), 1, CV_AA );
    circle( centrs, Point2f(centroids.second[0],centroids.second[1])+Point2f(window_width,0), 3, Scalar(0,0,255), 1, CV_AA );
    imshow("Centroids", centrs);

    waitKey(0);

    destroyWindow( "Matching Correspondences" );
    destroyWindow( "SIFT matches" );
    destroyWindow( "Centroids" );

    waitKey(10);

    return corrs;

}

match calcCentroids( const matchList& corrs, const Mat& img1, const Mat& img2 ) {

    printf( "\n\nCalculating the centroids for each cloud of correspondences\n" );

    Vec3f centr1(0,0,0), centr2(0,0,0);
    float z1, z2;

    for( int i = 0; i < (int)corrs.size(); i ++ ) {

        // First image 
        centr1[0] += corrs[i].first[0]; 
        centr1[1] += corrs[i].first[1]; 
        //z1 = (float)depthCV[0].at<short>( (int)centr1[0], (int)centr1[1] );
        z1 = getDepth( 0, (int)corrs[i].first[0], (int)corrs[i].first[1] );
        centr1[2] += z1; 

        // Second image
        centr2[0] += corrs[i].second[0];
        centr2[1] += corrs[i].second[1];
        //z1 = (float)depthCV[1].at<short>( (int)centr2[0], (int)centr2[1] );
        z2 = getDepth( 1, (int)corrs[i].second[0], (int)corrs[i].second[1] );
        centr2[2] += z2; 

        printf( "First %d: (%f,%f,%f)\n", i, corrs[i].first[0], corrs[i].first[1], z1 );
        printf( "Second %d: (%f,%f,%f)\n", i, corrs[i].second[0], corrs[i].second[1], z2 );
    }

    printf(" corrs.size() = %u\n", corrs.size() );
    // first
    centr1[0] /= corrs.size();
    centr1[1] /= corrs.size();
    centr1[2] /= corrs.size();
    // second
    centr2[0] /= corrs.size();
    centr2[1] /= corrs.size();
    centr2[2] /= corrs.size();

    match centroids( centr1, centr2 );

#if 0
    printf( "\nCorrespondence points in Image 1\n" );
    for( int i = 0; i < (int)corrs.size(); i++ ) 
        printf( "%d: (%f,%f,%f)\n", i, corrs[i].first[0], corrs[i].first[1], z1 );
    printf( "\nCorrespondence points in Image 2\n" );
    for( int i = 0; i < (int)corrs.size(); i++ ) 
        printf( "%d: (%f,%f,%f)\n", i, corrs[i].second[0], corrs[i].second[1], z2 );
#endif


    printf("\nCentroid 1 (x,y,z) = (%f,%f,%f)\n", centr1[0], centr1[1], centr1[2]);
    printf("Centroid 2 (x,y,z) = (%f,%f,%f)\n\n", centr2[0], centr2[1], centr2[2]);

    return centroids;
} 

void calcRotation( match centr, matchList corrs ) {

    Mat P = Mat::ones( 3, corrs.size(), CV_32F );
    Mat Q = Mat::ones( 3, corrs.size(), CV_32F );

    // This is retarded... Need to restructure
    for( int pt = 0; pt < corrs.size(); pt++ ) {
        // first point cloud
        float x = corrs[pt].first[0] / 640.; 
        float y = corrs[pt].first[1] / 480.;
        //float d = (float)depthCV[0].at<short>( (int)x, (int)y );
        float d = getDepth( 0, (int)x, (int)y ) / 2048.;
        P.at<float>(0,pt) = x-centr.first[0] / 640.; 
        P.at<float>(1,pt) = y-centr.first[1] / 480.; 
        P.at<float>(2,pt) = d; 
        // second point cloud
        float x2 = corrs[pt].second[0] / 640.; 
        float y2 = corrs[pt].second[1] / 480.;
        //float d2 = (float)depthCV[1].at<short>( (int)x2, (int)y2 );
        float d2 = getDepth( 1, (int)x2, (int)y2 ) / 2048.;
        Q.at<float>(0,pt) = x2-centr.second[0] / 640.; 
        Q.at<float>(1,pt) = y2-centr.second[1] / 480.; 
        Q.at<float>(2,pt) = d2; 
    }

    Mat Qt;
    transpose( Q, Qt );
    Mat PQt = P*Qt;

    printf(" P.rows = %d\n", P.rows );
    printf(" P.cols = %d\n\n", P.cols );

    printMat( P );	

    printf("\n Q.rows = %d\n", Q.rows );
    printf(" Q.cols = %d\n", Q.cols );

    printMat( Q );	

    printf(" PQt.rows = %d\n", PQt.rows );
    printf(" PQt.cols = %d\n", PQt.cols );

    SVD svd( PQt );

    printf(" svd.u.rows = %d\n", svd.u.rows );
    printf(" svd.u.cols = %d\n", svd.u.cols );

    printMat( svd.u );	

    printf(" svd.vt.rows = %d\n", svd.vt.rows );
    printf(" svd.vt.cols = %d\n", svd.vt.cols );

    printMat( svd.vt );	

    Mat r = svd.u*svd.vt;

    printf(" r.rows = %d\n", r.rows );
    printf(" r.cols = %d\n", r.cols );

    printMat( r );	

    // translations ( first point cloud to second )
    float x = centroids.second[0]-centroids.first[0]; 
    x /= 640.; 
    float y = centroids.second[1]-centroids.first[1]; 
    y /= 480.;
    float z = centroids.second[2]-centroids.first[2]; 
    z /= 2048.;

    // Make the complete transformation matrix ( rotation + translation ) 'press a'
    float rtdata[16] = { r.at<float>(0,0), r.at<float>(0,1), r.at<float>(0,2), x,
        r.at<float>(1,0), r.at<float>(1,1), r.at<float>(1,2), y,
        r.at<float>(2,0), r.at<float>(2,1), r.at<float>(2,2), z,
        0, 0, 0, 1 };
    Mat RT( 4, 4, CV_32F, rtdata );
    rottrans = RT.clone();
    printf("rottrans: "); 
    printMat(rottrans); 

    // Make the rotation matrix 'press r'
    float rdata[16] = { r.at<float>(0,0), r.at<float>(0,1), r.at<float>(0,2), 0,
        r.at<float>(1,0), r.at<float>(1,1), r.at<float>(1,2), 0,
        r.at<float>(2,0), r.at<float>(2,1), r.at<float>(2,2), 0,
        0, 0, 0, 1 };
    Mat R( 4, 4, CV_32F, rdata );
    rot = R.clone();
    printf("rot: "); 
    printMat(rot); 

    // Make the translation matrix 'press t'
    float tdata[16] = { 1, 0, 0, x, 
        0, 1, 0, y,
        0, 0, 1, z,
        0, 0, 0, 1 };
    Mat T( 4, 4, CV_32F, tdata );
    trans = T.clone();
    printf("trans: "); 
    printMat(trans); 
}

void printMat( const Mat& A ) {

    printf("\n| ");
    for(int i = 0; i < A.rows; i++) {
        if(i > 0 && i < A.rows) {
            printf("|\n");
            printf("| ");
        }
        for(int j = 0; j < A.cols; j++) {
            printf( "%f ", A.at<float>(i,j) );
        }
    }
    for(int i = 0; i < A.rows; i++) {
        if(i > 0 && i < A.rows) {
            printf("|\n");
            printf("| ");
        }
        for(int j = 0; j < A.cols; j++) {
            printf( "%f ", A.data[i*A.step + j] );
        }
    }
    printf("|\n");
}

// Definitely need to come up with a better
// way to do this...
float getDepth( int cam, int x, int y ) {

    float d = (float)(depthCV[cam].at<short>(x,y) & MASK);
    printf("short, float = %d, %f\n",depthCV[cam].at<short>(x,y) & MASK, d);

    if( d >= 2047 ) 
        d = (float)(depthCV[cam].at<short>(x,y+1) & MASK);

    if( d >= 2047 ) 
        d = (float)(depthCV[cam].at<short>(x,y-1) & MASK);

    if( d >= 2047 ) 
        d = (float)(depthCV[cam].at<short>(x+1,y) & MASK);

    if( d >= 2047 ) 
        d = (float)(depthCV[cam].at<short>(x-1,y) & MASK);

    if( d >= 2047 )
        d = (float)(depthCV[cam].at<short>(x-1,y-1) & MASK);

    if( d >= 2047 )
        d = (float)(depthCV[cam].at<short>(x-1,y+1) & MASK);

    if( d >= 2047 )
        d = (float)(depthCV[cam].at<short>(x+1,y-1) & MASK);

    if( d >= 2047 )
        d = (float)(depthCV[cam].at<short>(x+1,y+1) & MASK);

    return d;
}

// Make the drawings pretty
void define_lights_and_materials()
{

    // Ambient: color in the absence of light
    glLightfv(GL_LIGHT0, GL_AMBIENT,  light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  light_diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);

    glMaterialfv(GL_FRONT, GL_AMBIENT,   mat_ambient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE,   mat_diffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR,  mat_specular);
    glMaterialfv(GL_FRONT, GL_SHININESS, high_shininess);

    glMaterialfv(GL_BACK, GL_AMBIENT,   mat_ambient_back);
    glMaterialfv(GL_BACK, GL_DIFFUSE,   mat_diffuse_back);
    glMaterialfv(GL_BACK, GL_SPECULAR,  mat_specular);
    glMaterialfv(GL_BACK, GL_SHININESS, high_shininess);

    // lighting only has effect if lighting is on
    // color of vertices have no effect without material 
    //   if phone is one (GL_SMOOTH)

    glEnable(GL_LIGHT0);
    //glEnable(GL_LIGHT_MODEL_TWO_SIDE);

    // I don't really undersand GL_COLOR_MATERIAL
    glEnable(GL_COLOR_MATERIAL);
    //glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE); // makes sure normal remain unit length

    // Blending is only active when blending mode is ON
    // Page 129 in GLUT course
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void draw_line( Vec3b v1, Vec3b v2) {

    glBegin(GL_LINES);
    glVertex3f(v1[0], v1[1], v1[2]);
    glVertex3f(v2[0], v2[1], v2[2]);
    glEnd();

}

void draw_axes() {

    //X Axis

    glColor3f(1,0,0);    //red
    Vec3b r1(0,0,0);
    Vec3b r2(1,0,0);
    draw_line(r1, r2);

    //Y Axis

    glColor3f(0,1,0);    //green
    Vec3b g1(0,0,0);
    Vec3b g2(0,1,0);
    draw_line(g1, g2);

    //Z Axis

    glColor3f(0,0,1);    //blue
    Vec3b b1(0,0,0);
    Vec3b b2(0,0,1);
    draw_line(b1, b2);

}






