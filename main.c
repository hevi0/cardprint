#include "png_dpi_util.h"

#include <assert.h>

#define _USE_MATH_DEFINES
#include <math.h>
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>




#ifdef CFLAG_APPNAME
    #define APPNAME() CFLAG_APPNAME
#else
    #define APPNAME() "app"
#endif
#define PAPERSIZE_PARAM_LEN 16
#define PPI_PARAM_LEN 8
#define MAX_PATHLEN 128
#define CARDS_PER_PAGE 9
#define MAX_NUM_PAGES 80 // Code assumes no more than 99 pages will be printed using this.
#define MAX_CARDS CARDS_PER_PAGE*MAX_NUM_PAGES
#define OUTPUT_SUFFIX_LEN 6 // The "XX.png" that comes after the output page name.
#define OUTPUT_PATHLEN MAX_PATHLEN - OUTPUT_SUFFIX_LEN

// Different corner radius exist for playing cards.
// 3mm ~ 0.11811in
// 3.5mm ~ 0.137795in
#define CORNER_RADIUS_INCH 0.11811
#define CARD_BORDER_INCH 0.11811

// See the DrawQuarterArc function for details about these numbers.
// They should work for either 3.5mm or 3mm corner radius.
#define NUM_POINTS_300 65
#define NUM_POINTS_600 130
#define NUM_POINTS_1200 260

#define ARC_THICKNESS_PIXELS 3
#define GUTTER_THICKNESS_PIXELS 3

static char CARD_IMAGE_FILENAMES[MAX_CARDS][MAX_PATHLEN];

static SDL_Point ARC_POINTS[NUM_POINTS_1200];


/**
 * ASSUMPTIONS
 * 
 * - Printing on 8.5x11 paper
 * - Card dimensions are 2.5in x 3.5in
 * - 9 cards in a 3x3 layout
 * - Have the cards already in the correct pixel dimensions. This will not do resize, only layout.
 */
enum PPI {
    ppi300 = 300,
    ppi600 = 600,
    ppi1200 = 1200
};

enum PaperSize {
    paperUS = 0, // 8.5 x 11 paper
    paperA4 = 1
};

typedef SDL_Rect CardShape;

/**
 * Returns the dimensions of a card based
 * on the PPI (pixels-per-inch).
 * The value returned is in pixels.
 */
CardShape GetCardShape(enum PPI ppi) {
    // Returns pixel dimensions based on 
    // 2.5in x 3.5in card sizes
    // Actual card sizes are 63mm x 88mm, which becomes approx. 2.48031in x 3.46457in

    //return (CardShape){ .x = 0, .y = 0, .w = ppi*2 + ppi/2, .h = ppi*3 + ppi/2 };
    return (CardShape){
        .x = 0,
        .y = 0,
        .w = (int)(ppi * 2.48031),
        .h = (int)(ppi * 3.46457)
    };
}


/**
 * Update num_points and radius_pixels parameters
 * with based on the chosen PPI (pixels-per-inch).
 */
void QuarterArcParams(enum PPI ppi, int* num_points, int* radius_pixels) {
    // See DrawQuarterArc function for explanation of values.
    switch (ppi) {
        case ppi600:
            (*num_points) = NUM_POINTS_600;
        case ppi1200:
            (*num_points) = NUM_POINTS_1200;
        default:
            (*num_points) = NUM_POINTS_300;
    }

    // Should end up as @300 DPI = 65, @600DPI = 130, @1200 DPI = 260 
    (*radius_pixels) = (int)roundf(CORNER_RADIUS_INCH * ppi);
}


/**
 * Get the page width in pixels.
 */
int PageWidth(enum PPI ppi, enum PaperSize paperSize) {

    switch(paperSize) {
        case paperA4:
            return (int)(ppi * 8.27);
        default:
            return ppi * 8 + ppi/2;
    }
    
}

/**
 * Get the page height in pixels.
 */
int PageHeight(enum PPI ppi, enum PaperSize paperSize) {
    // Assume 11 inches
    switch(paperSize) {
        case paperA4:
            return (int)(ppi * 11.69);
        default:
            return ppi * 11;
    }
    
}

/**
 * Calculate the horizontal margins, which are
 * evenly distributed between the left and right.
 * The margins are calculated from the content dimensions
 * and paper dimensions. Value returned is in pixels.
 */
int MarginHoriz(enum PPI ppi, enum PaperSize paperSize, CardShape cardShape) {
    // Assuming around 0.5in top and bottom margins
    // on a 8.5x11 paper
    int page_width_pixels = PageWidth(ppi, paperSize);
    int content_width_pixels = 3*cardShape.w;
    return (page_width_pixels - content_width_pixels)/2;
}

/**
 * Calculate the vertical margins, which are
 * evenly distributed between the top and bottom.
 * The margin are calculated from the content dimensions
 * and paper dimensions. Value returned is in pixels.
 */
int MarginVert(enum PPI ppi, enum PaperSize paperSize, CardShape cardShape) {
    // Assuming around 0.25in top and bottom margins
    // on a 8.5x11 paper

    int page_height_pixels = PageHeight(ppi, paperSize);
    int content_height_pixels = 3*cardShape.h;
    return (page_height_pixels - content_height_pixels)/2;
}

/**
 * Take in a number, 0-8, representing the slot
 * in the 3x3 grid that the card will be placed.
 * Return the x,y coord for the top-left corner
 * that the card should be.
 * 
 * Position and dimensions returned are in pixels.
 */
CardShape CardPlacement(int pos, enum PPI ppi, enum PaperSize paperSize) {
    
    assert(pos >= 0 && pos <= 8);
    CardShape card = GetCardShape(ppi);
    
    int col = 0;
    if (pos == 1 || pos == 4 || pos == 7) {
        col = 1;
    }
    else if (pos == 2 || pos == 5 || pos == 8) {
        col = 2;
    }

    int row = 0;
    if (pos == 3 || pos == 4 || pos == 5) {
        row = 1;
    }
    else if (pos == 6 || pos == 7 || pos == 8) {
        row = 2;
    }

    int gutterCol = (col+1)*GUTTER_THICKNESS_PIXELS;
    int gutterRow = (row+1)*GUTTER_THICKNESS_PIXELS;

    card.x = (col*card.w) + gutterCol + MarginHoriz(ppi, paperSize, card);
    card.y = (row*card.h) + gutterRow + MarginVert(ppi, paperSize, card);

    return card;
}

/**
 * Draw the guide/gutter lines that extend outside the
 * content area containing the cardgrid and margins.
 * These lines will be the same thickness as the gutter
 * lines.
 */
void DrawBackgroundLines(SDL_Renderer* renderer, SDL_Color color, enum PPI ppi, enum PaperSize paperSize) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    CardShape cardShape = GetCardShape(ppi);

    for (int i = 0; i < 4; ++i) {
        int gutter = (i)*GUTTER_THICKNESS_PIXELS;

        SDL_Rect rect = {
            .x = (cardShape.w*i) + gutter + MarginHoriz(ppi, paperSize, cardShape),
            .y = 0,
            .w = GUTTER_THICKNESS_PIXELS,
            .h = PageHeight(ppi, paperSize)
        };

        SDL_RenderFillRect(renderer, &rect);
    }

    for (int i = 0; i < 4; ++i) {
        int gutter = (i)*GUTTER_THICKNESS_PIXELS;

        SDL_Rect rect = {
            .x = 0,
            .y = (cardShape.h*i) + gutter + MarginVert(ppi, paperSize, cardShape),
            .w = PageWidth(ppi, paperSize),
            .h = GUTTER_THICKNESS_PIXELS
        };

        SDL_RenderFillRect(renderer, &rect);
    }
}

/**
 * Fill in the gutters between cards with the chosen color.
 * The gutter is intended to give some extra wiggle room
 * when cutting.
 */
void DrawGutterLines(SDL_Renderer* renderer, SDL_Color color, enum PPI ppi, enum PaperSize paperSize) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    CardShape cardShape = GetCardShape(ppi);

    for (int i = 0; i < 4; ++i) {
        int gutter = (i)*GUTTER_THICKNESS_PIXELS;

        SDL_Rect rect = {
            .x = (cardShape.w*i) + gutter + MarginHoriz(ppi, paperSize, cardShape),
            .y = MarginVert(ppi, paperSize, cardShape),
            .w = GUTTER_THICKNESS_PIXELS,
            .h = 3*cardShape.h + 4*GUTTER_THICKNESS_PIXELS
        };

        SDL_RenderFillRect(renderer, &rect);
    }

    for (int i = 0; i < 4; ++i) {
        int gutter = (i)*GUTTER_THICKNESS_PIXELS;

        SDL_Rect rect = {
            .x = MarginHoriz(ppi, paperSize, cardShape),
            .y = (cardShape.h*i) + gutter + MarginVert(ppi, paperSize, cardShape),
            .w = 3*cardShape.w + 4*GUTTER_THICKNESS_PIXELS,
            .h = GUTTER_THICKNESS_PIXELS
        };

        SDL_RenderFillRect(renderer, &rect);
    }
}

/**
 * Intended to be used for drawing the rounded corner lines.
 * For standard playing cards, the corner radius is often 3.5mm (0.137795 inches).
 * This radius translates to the following number of pixels at 300, 600, and 1200 DPI:
 * @300 DPI = 41.3385 pixel radius
 * @600 DPI = 82.677 pixel radius
 * @1200 DPI = 165.354 pixel radius
 * 
 * These pixel radiuses create circles of the following number of pixels:
 * @41.3385 pixel radius ~ 259.73746 pixel circumference
 * @82.677 pixel radius ~ 519.47491 pixel circumference
 * @165.354 pixel radius ~ 1038.94982 pixel circumference
 * 
 * Finally, since we're only concerned with pi/2 arcs we should be able
 * to divide these circumferences by 4 and get a good idea what the number
 * of points should be (rounded-up):
 * @300 DPI ~ 259.73746 / 4 ~ 65
 * @600 DPI ~ 519.47491 / 4 ~ 130
 * @1200 DPI ~ 1038.94982 / 4 ~ 260
 * 
 */
void DrawQuarterArc(SDL_Renderer* renderer, SDL_Color color, int c_x, int c_y, int quad, enum PPI ppi) {
    // Figure out the right quadrant of the circle we are drawing.
    assert(quad >= 0 && quad <= 3);

    float start_angle_rad = 0.0;
    float end_angle_rad = M_PI;

    switch(quad) {
        case 0:
            start_angle_rad = 0.0;
            end_angle_rad = M_PI/2.0;
            break;
        case 1:
            start_angle_rad = M_PI/2.0;
            end_angle_rad = M_PI;
            break;
        case 2:
            start_angle_rad = M_PI;
            end_angle_rad = 3*M_PI/2.0;
            break;
        case 3:
            start_angle_rad = 3*M_PI/2.0;
            end_angle_rad = 2*M_PI;
            break;
    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    // Figure out the radius and line segments for the arc,
    // which is based on the pixels-per-inch.
    // The more segments, the smoother the arc will look.
    int num_segments = 0;
    int radius = 0;
    QuarterArcParams(ppi, &num_segments, &radius);

    /**
     * The outer loop causes redraws of the arc at different radiuses.
     * Good enough and simple for making the arc visible in the print
     * without using more math.
     * 
     * Could be done by drawing propoerly rotated rectangles, I suppose.
     */
    for (int i = -ARC_THICKNESS_PIXELS/2; i <= ARC_THICKNESS_PIXELS/2; ++i) {

        // Draw the arc
        for (int j = 0; j < num_segments; ++j) {
            float angle = start_angle_rad + (end_angle_rad - start_angle_rad) * j / (float)(num_segments - 1);
            ARC_POINTS[j].x = c_x + (int)((radius+i) * cos(angle));
            ARC_POINTS[j].y = c_y - (int)((radius+i) * sin(angle)); // Use subtraction to adjust for inverted-y coordinates
        }

        SDL_RenderDrawLines(renderer, ARC_POINTS, num_segments);

    }
    
}

/**
 * For a particular card at a position in the
 * content area, draw the arcs representing the
 * rounded corners.
 */
void DrawRoundedCorners(SDL_Renderer* renderer, SDL_Color color, int pos, enum PPI ppi, enum PaperSize paperSize) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    CardShape cardShape = CardPlacement(pos, ppi, paperSize);
    int radius_pixels = (int)roundf(CORNER_RADIUS_INCH * ppi);

    // top-left
    DrawQuarterArc(renderer, color, cardShape.x+radius_pixels, cardShape.y+radius_pixels, 1, ppi);

    // top-right
    DrawQuarterArc(renderer, color, cardShape.x+cardShape.w-radius_pixels, cardShape.y+radius_pixels, 0, ppi);

    // bottom-right
    DrawQuarterArc(renderer, color, cardShape.x+cardShape.w-radius_pixels, cardShape.y+cardShape.h-radius_pixels, 3, ppi);

    // bottom-left
    DrawQuarterArc(renderer, color, cardShape.x+radius_pixels, cardShape.y+cardShape.h-radius_pixels, 2, ppi);
}

void DrawBlankCardBorder(SDL_Renderer* renderer, SDL_Color color, int pos, enum PPI ppi, enum PaperSize paperSize) {
    
    // See DrawMarginBorder for comment about how 
    // the rectangles are drawn.
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    CardShape cardShape = CardPlacement(pos, ppi, paperSize);

    int border_pixels = (int) (ppi * CARD_BORDER_INCH/2.0);

    SDL_Rect rects[4] = {
        // Top rectangle
        {
            cardShape.x,
            cardShape.y,
            cardShape.w,
            border_pixels
        },

        // Right rectangle
        {
            cardShape.x + cardShape.w - border_pixels,
            cardShape.y + border_pixels,
            border_pixels,
            cardShape.h - 2*border_pixels
        },

        // Bottom rectangle
        {
            cardShape.x,
            cardShape.y + cardShape.h - border_pixels,
            cardShape.w,
            border_pixels
        },

        // Left Rectangle
        {
            cardShape.x,
            cardShape.y + border_pixels,
            border_pixels,
            cardShape.h - 2*border_pixels
        }

    };

    SDL_RenderFillRects(renderer, rects, 4);
}

void DrawMarginBorder(SDL_Renderer* renderer, SDL_Color color, enum PPI ppi, enum PaperSize paperSize) {
    /**
     * Draws the 4 rectangles for the margin color
     * surrounding the 9-card content area:
     * 
     *   111111111
     *   4 X X X 2
     *   4 X X X 2
     *   4 X X X 2
     *   333333333
     * 
     * The top and bottom rectangles will extend beyond 
     * the content width by the CARD_BORDER_INCH on each side.
     * 
     * The left and right rectangles will have a width of the
     * CARD_BORDER_INCH and will have a height equal to the
     * content height.
     * 
     * The color should be the same as the card background color.
     * 
     */
    
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    CardShape cardShape = GetCardShape(ppi);
    
    int horiz = MarginHoriz(ppi, paperSize, cardShape);
    int vert = MarginVert(ppi, paperSize, cardShape);
    int border_pixels = (int) (ppi * CARD_BORDER_INCH/2.0);
    int total_gutters = 4*GUTTER_THICKNESS_PIXELS;

    SDL_Rect rects[4] = {
        // Top rectangle
        {
            horiz - border_pixels, // x
            vert - border_pixels, // y
            3*cardShape.w + 2*border_pixels + total_gutters, // w
            border_pixels // h
        },

        // Right rectangle
        {
            horiz + 3*cardShape.w + total_gutters,
            vert,
            border_pixels,
            3*cardShape.h + total_gutters
        },

        // Bottom rectangle
        {
            horiz - border_pixels,
            vert + 3*cardShape.h + total_gutters,
            3*cardShape.w + 2*border_pixels + total_gutters,
            border_pixels
        },

        // Left rectangle
        {
            horiz - border_pixels,
            vert,
            border_pixels,
            3*cardShape.h + total_gutters
        }
    };

    SDL_RenderFillRects(renderer, rects, 4);
}

SDL_Surface* LoadCardImage(const char* filename, SDL_Color bgcolor, enum PPI ppi) {
    SDL_Surface* image = NULL;
    assert(filename != NULL);
    assert(strlen(filename) >= 1);

    image = IMG_Load(filename);
    if (image == NULL)
        return NULL;

    CardShape targetRect = GetCardShape(ppi);
    CardShape sourceRect = { .x = 0, .y = 0, .w = image->w, .h = image-> h };
    SDL_Surface* postProcessedImage = SDL_CreateRGBSurface(0, targetRect.w, targetRect.h, 32, 0, 0, 0, 0);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(postProcessedImage);
    SDL_Rect bgRect = { .x = 0, .y = 0, .w = targetRect.w, .h = targetRect.h };

    SDL_SetRenderDrawColor(renderer, bgcolor.r, bgcolor.g, bgcolor.b, bgcolor.a);
    SDL_RenderFillRect(renderer, &bgRect);
    SDL_DestroyRenderer(renderer);

    SDL_BlitScaled(image, &sourceRect, postProcessedImage, &targetRect);

    SDL_FreeSurface(image);
    return postProcessedImage;
}

void AddCardToPage(SDL_Surface* pageImage, SDL_Surface* cardImage, int pos, enum PPI ppi, enum PaperSize paperSize) {
    assert(pageImage != NULL);
    assert(cardImage != NULL);
    assert(pos >= 0 && pos < 9);
    
    CardShape sourceRect = { .x = 0, .y = 0, .w = cardImage->w, .h = cardImage->h };
    CardShape targetRect = CardPlacement(pos, ppi, paperSize);

    SDL_BlitScaled(cardImage, &sourceRect, pageImage, &targetRect);
}

/**
 * Trim left. Update in-place.
 */
void TrimLeft(char* s, size_t n) {
    assert(strlen(s) < n);
    int i = 0;
    while (s[i] != '\0' && i < n-1 && isspace(s[i])) {
        i++;
    }
    if (i == 0)
        return;
        
    int skipped = i;
    while (s[i] != '\0' && i < n-1) {
        s[i-skipped] = s[i];
        i++;
    }
    s[i-skipped] = '\0';
}

/**
 * Trim right. Update in-place.
 */
void TrimRight(char* s, size_t n) {
    assert(strlen(s) < n);

    int i = strlen(s)-1;
    while (i >= 0 && (isspace(s[i]) || s[i] == '\0')) {
        i--;
    }
    if (i == n-1)
        return;
        
    s[i+1] = '\0';
}

/**
 * Trim left and right. Update in-place.
 */
void Trim(char* s, size_t n) {
    TrimLeft(s, n);
    TrimRight(s, n);
}

/**
 * Updates the ppi param and returns true if the string provided
 * can be parsed into an integer.
 */
bool ParsePPI(char* s, enum PPI* ppi, size_t n) {
    assert(strlen(s) < n);
    int i = strtol(s, NULL, 10);
    switch (i) {
        case 300:
            (*ppi) = 300;
            break;
        case 600:
            (*ppi) = 600;
            break;
        case 1200:
            (*ppi) = 1200;
            break;

        default:
            return false;
    }

    return true;
}

/**
 * Updates the color param and returns true if the string provided
 * can be parsed into RGBA color values.
 */
bool ParseSDLColor(char* s, SDL_Color* color, size_t n) {
    assert(strlen(s) < n-1);
    int nums[4];

    int i = 0;
    int spaceCount = 0;
    while (s[i] != '\0' && i < n-1) {
        if (isdigit(s[i])) {
            i++;
        }
        else if (s[i] == ' ') {
            spaceCount++;
            i++;
        }
        else {
            printf("Encountered an invalid character parsing color values in: [%s]\n", s);
            return false;
        }
            
    }

    if (spaceCount != 3) {
        printf("Should be 3 spaces in %s\n", s);
        return false;
    }
    
    char* ptr = s;
    for (int i = 0; i < 4; i++) {
        nums[i] = strtol(ptr, &ptr, 10);
        if (nums[i] < 0 || nums[i] > 255) {
            printf("Invalid value in: [%s]\n", s);
            return false;
        }
            
    }

    color->r = nums[0];
    color->g = nums[1];
    color->b = nums[2];
    color->a = nums[3];

    return true;
}

bool ParsePaperSize(char* s, enum PaperSize* paperSize, size_t n) {
    if (strncmp("US", s, n) == 0) {
        (*paperSize) = 0;
        return true;
    }
    else if (strncmp("A4", s, n) == 0) {
        (*paperSize) = 1;
        return true;
    }

    return false;
    
}

int LoadConfig(
    char* filename, 
    enum PPI* ppi, 
    SDL_Color* cardBGColor, 
    SDL_Color* cardLineColor,
    int* roundedcorners, 
    enum PaperSize* paperSize,
    char cards[CARDS_PER_PAGE][MAX_PATHLEN]) {

    assert(strlen(filename) > 0);

    FILE* f = NULL;
    f = fopen(filename, "r");
    if (!f) {
        printf("Couldn't read %s\n", filename);
        return -1;
    }

    int currCard = 0;
    char line[MAX_PATHLEN];

    fgets(line, MAX_PATHLEN, f);
    Trim(line, MAX_PATHLEN);
    while(strlen(line) == 0 || line[0] == '#') {
        fgets(line, MAX_PATHLEN, f);
        Trim(line, MAX_PATHLEN);
    }
    
    if (!ParsePaperSize(line, paperSize, MAX_PATHLEN)) {
        printf("Invalid paper size: %s\n", line);
        fclose(f);
        return -1;
    }

    fgets(line, MAX_PATHLEN, f);
    Trim(line, MAX_PATHLEN);
    while(strlen(line) == 0 || line[0] == '#') {
        fgets(line, MAX_PATHLEN, f);
        Trim(line, MAX_PATHLEN);
    }
    if (!ParsePPI(line, ppi, MAX_PATHLEN)) {
        printf("Could not parse PPI from: %s\n", line);
        fclose(f);
        return -1;
    }

    fgets(line, MAX_PATHLEN, f);
    Trim(line, MAX_PATHLEN);
    while(strlen(line) == 0 || line[0] == '#') {
        fgets(line, MAX_PATHLEN, f);
        Trim(line, MAX_PATHLEN);
    }
    if (!ParseSDLColor(line, cardBGColor, MAX_PATHLEN)) {
        printf("Could not parse BG color for cards\n");
        fclose(f);
        return -1;
    }
    
    fgets(line, MAX_PATHLEN, f);
    Trim(line, MAX_PATHLEN);
    while(strlen(line) == 0 || line[0] == '#') {
        fgets(line, MAX_PATHLEN, f);
        Trim(line, MAX_PATHLEN);
    }
    if (!ParseSDLColor(line, cardLineColor, MAX_PATHLEN)) {
        printf("Could not parse line color for cards\n");
        fclose(f);
        return -1;
    }
    
    fgets(line, MAX_PATHLEN, f);
    Trim(line, MAX_PATHLEN);
    while(strlen(line) == 0 || line[0] == '#') {
        fgets(line, MAX_PATHLEN, f);
        Trim(line, MAX_PATHLEN);
    }
    if (strlen(line) != 1) {
        printf("Could not parse rounded corner toggle (should be 0 or 1)\n");
        fclose(f);
        return -1;
    }
    if (line[0] == '0')
        (*roundedcorners) = 0;
    else if (line[0] == '1')
        (*roundedcorners) = 1;
    else {
        printf("Could not parse rounded corner toggle (should be 0 or 1)\n");
        fclose(f);
        return -1;
    }

    while (currCard < MAX_CARDS && fgets(line, MAX_PATHLEN, f)) {
        Trim(line, MAX_PATHLEN);
        if (strlen(line) > 0 && line[0] != '#') {
            strncpy(cards[currCard], line, MAX_PATHLEN);
            currCard++;
        }
    }

    fclose(f);
    return currCard;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Create sheets of cards arranged 3x3.\n");
        printf("Input is a text file. See the test.txt example.\n");
        printf("Output will be png [OUTPUT_PREFIX]XX.png. XX is the page number.\n");
        printf("PAPER_SIZE AND PPI override any values defined in the input file.\n\n");
        printf("Usage: %s INPUT_FILE [OUTPUT_PREFIX (default \"page\")] [PPI (300|600|1200) (default 300)] [PAPER_SIZE (A4|US) (default US)]\n", APPNAME());
        exit(1);
    }

    if (strlen(argv[1]) >= MAX_PATHLEN) {
        printf("Path of input must be less than %d\n", MAX_PATHLEN);
        exit(1);
    }
    char* inputFilename = argv[1];

    char outputPrefix[OUTPUT_PATHLEN-1] = "page";
    if (argc >= 3) {
        if (strlen(argv[2]) >= OUTPUT_PATHLEN) {
            printf("Path of output must be less than %d\n", OUTPUT_PATHLEN);
            exit(1);
        }
        strncpy(outputPrefix, argv[2], OUTPUT_PATHLEN-1);
    }
    
    char globalPPI[PPI_PARAM_LEN] = "";
    if (argc >= 4) {
        if (strlen(argv[3]) >= PPI_PARAM_LEN) {
            printf("PPI is invalid: %s.\nOnly 300, 600, 1200 are accepted.", argv[3]);
            exit(1);
        }
        strncpy(globalPPI, argv[3], PPI_PARAM_LEN);
    }

    char globalPaperSize[PAPERSIZE_PARAM_LEN] = "";
    if (argc >= 5) {
        if (strlen(argv[4]) >= PAPERSIZE_PARAM_LEN) {
            printf("Paper size is invalid: %s.\nOnly US and A4 are accepted.", argv[4]);
            exit(1);
        }
        strncpy(globalPaperSize, argv[4], PAPERSIZE_PARAM_LEN);
    }
    
    enum PPI ppi = ppi600;
    SDL_Color cardBGColor = { .r = 255, .g = 255, .b = 255, .a = 255 };
    SDL_Color cardLines = { .r = 128, .g = 128, .b = 128, .a = 255 };
    int roundedCorners = 0;
    enum PaperSize paperSize = paperUS;

    printf("Loading %s\n", inputFilename);
    int cardCount = LoadConfig(inputFilename, &ppi, &cardBGColor, &cardLines, &roundedCorners, &paperSize, CARD_IMAGE_FILENAMES);
    assert(cardCount <= MAX_CARDS);
    if (cardCount == -1) {
        printf("Config error. Check %s\n", inputFilename);
        exit(1);
    }

    // Override paper size with command-line parameter.
    if (strlen(globalPaperSize) > 0) {
        if (!ParsePaperSize(globalPaperSize, &paperSize, PAPERSIZE_PARAM_LEN)) {
            printf("Config error. Check PAPER_SIZE command-line parameter.\n");
            exit(1);
        }
    }

    // Override PPI with command-line parameter.
    if (strlen(globalPPI) > 0) {
        if (!ParsePPI(globalPPI, &ppi, PPI_PARAM_LEN)) {
            printf("Config error. Check PPI command-line parameter.\n");
            exit(1);
        }
    }

    switch (paperSize) {
        case 0:
            printf("US\n");
            break;
        case 1:
            printf("A4\n");
            break;
    }
    printf("%d\n", ppi);
    printf("Background line color: %d %d %d %d\n", cardBGColor.r, cardBGColor.g, cardBGColor.b, cardBGColor.a);
    printf("Gutter line color: %d %d %d %d\n", cardLines.r, cardLines.g, cardLines.b, cardLines.a);
    printf("Rounded corners: %d\n", roundedCorners);

    int pageCount = cardCount/CARDS_PER_PAGE + (cardCount%CARDS_PER_PAGE == 0 ? 0 : 1);
    printf("Generating %d pages\n", pageCount);

    SDL_Surface* page = SDL_CreateRGBSurface(0, PageWidth(ppi,paperSize), PageHeight(ppi,paperSize), 32, 0, 0, 0, 0);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(page);
    
    int currPage = 0;
    while (currPage < pageCount) {
        printf("Building page %02d with:\n", currPage+1);
        for (int i = currPage*CARDS_PER_PAGE; i < cardCount && i < (currPage+1)*CARDS_PER_PAGE ; i++) {
            printf("%d. %s\n", i+1, CARD_IMAGE_FILENAMES[i]);
        }

        // Start with a background
        SDL_Rect pageBGRect = { .x = 0, .y = 0, .w = PageWidth(ppi,paperSize), .h = PageHeight(ppi,paperSize) };
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &pageBGRect);

        // Extend the card background color into the margin by
        // an amount equal to the CARD_BORDER_INCH (around 3-3.5 mm)
        // Gives a little more room for error when cutting.
        DrawMarginBorder(renderer, cardBGColor, ppi, paperSize);

        // Simple gray lines for basic alignment helpers (registers)
        SDL_Color bgLines = { .r = 64, .g = 64, .b = 64, .a = 255 };
        DrawBackgroundLines(renderer, bgLines, ppi, paperSize);

        int cardsOnPageCount = 0;
        for (int i = currPage*CARDS_PER_PAGE; i < cardCount && i < (currPage+1)*CARDS_PER_PAGE ; i++) {
            SDL_Surface* cardImage = LoadCardImage(CARD_IMAGE_FILENAMES[i], cardBGColor, ppi);
            if (cardImage == NULL) {
                printf("Error reading %s\n", CARD_IMAGE_FILENAMES[i]);
                printf("%s\n", SDL_GetError());
                continue;
            }
            else {
                printf("Adding %s to page %02d\n", CARD_IMAGE_FILENAMES[i], currPage+1);
            }

            AddCardToPage(page, cardImage, i%CARDS_PER_PAGE, ppi, paperSize);
            cardsOnPageCount++;
            SDL_FreeSurface(cardImage);
        }

        // Fill all blank card positions with an inner border
        // equal to the card background color chosen.
        // Similarly to the margin border, this is
        // to help make cutting easier.
        for (int j = cardsOnPageCount; j < 9; ++j) {
            DrawBlankCardBorder(renderer, cardBGColor, j, ppi, paperSize);
        }

        DrawGutterLines(renderer, cardLines, ppi, paperSize);

        if (roundedCorners) {
            for (int i = currPage*CARDS_PER_PAGE; i < cardCount && i < (currPage+1)*CARDS_PER_PAGE ; i++) {
                DrawRoundedCorners(renderer, cardLines, i%CARDS_PER_PAGE, ppi, paperSize);
            }
        }

        char outputFilename[MAX_PATHLEN];
        sprintf(outputFilename, "%s%02d.png", outputPrefix, currPage+1);
        IMG_SavePNG(page, outputFilename);
        update_png_dpi(outputFilename, ppi);
        SDL_RenderClear(renderer);
        currPage++;
    }
    
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(page);    
}
