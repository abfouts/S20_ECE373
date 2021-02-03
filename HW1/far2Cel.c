#include "stdio.h"
#include "stdlib.h"

//Simple fahrenheit to celsius program
float main(int argc, char *argv[]) { 
    char *temp = argv[1];
    float fahrenheit = atoi(temp);
    float celsius = ((fahrenheit - 32) / 1.8);

    printf("Fahrenheit = %f\nCelcius = %f\n", fahrenheit, celsius);
    return celsius;
}