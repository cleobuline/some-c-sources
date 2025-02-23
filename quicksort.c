#include <stdio.h>
#include <string.h>

void swap(char **a, char **b) {
    char *temp = *a;
    *a = *b;
    *b = temp;
}

int partition(char *arr[], int low, int high) {
    char *pivot = arr[high];
    int i = (low - 1);

    for (int j = low; j < high; j++) {
        if (strcmp(arr[j], pivot) < 0) {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[high]);
    return (i + 1);
}

void quickSort(char *arr[], int low, int high) {
    if (low < high) {
        int pi = partition(arr, low, high);

        quickSort(arr, low, pi - 1);
        quickSort(arr, pi + 1, high);
    }
}

void printArray(char *arr[], int n) {
    for (int i = 0; i < n; i++)
        printf("%s\n", arr[i]);
}

int main() {
    char *arr[] = {"pomme", "banane", "orange", "ananas", "kiwi", "raisin"};
    int n = sizeof(arr) / sizeof(arr[0]);

    printf("Tableau avant tri:\n");
    printArray(arr, n);

    quickSort(arr, 0, n - 1);

    printf("\nTableau après tri:\n");
    printArray(arr, n);

    return 0;
}
