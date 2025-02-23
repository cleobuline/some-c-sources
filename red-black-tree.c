#include <stdio.h>
#include <stdlib.h>

typedef enum { RED, BLACK } Color;

typedef struct Node {
    int data;
    Color color;
    struct Node* parent;
    struct Node* left;
    struct Node* right;
} Node;

typedef struct RedBlackTree {
    Node* root;
} RedBlackTree;

Node* createNode(int data, Color color, Node* parent, Node* left, Node* right) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    if (newNode == NULL) {
        perror("Erreur lors de l'allocation de mémoire pour le nœud.");
        exit(EXIT_FAILURE);
    }

    newNode->data = data;
    newNode->color = color;
    newNode->parent = parent;
    newNode->left = left;
    newNode->right = right;

    return newNode;
}

void leftRotate(RedBlackTree* tree, Node* x) {
    Node* y = x->right;
    x->right = y->left;
    if (y->left != NULL) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == NULL) {
        tree->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

void rightRotate(RedBlackTree* tree, Node* y) {
    Node* x = y->left;
    y->left = x->right;
    if (x->right != NULL) {
        x->right->parent = y;
    }
    x->parent = y->parent;
    if (y->parent == NULL) {
        tree->root = x;
    } else if (y == y->parent->left) {
        y->parent->left = x;
    } else {
        y->parent->right = x;
    }
    x->right = y;
    y->parent = x;
}

void insertFixup(RedBlackTree* tree, Node* z) {
    while (z != tree->root && z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            Node* y = z->parent->parent->right;
            if (y != NULL && y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    leftRotate(tree, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rightRotate(tree, z->parent->parent);
            }
        } else {
            Node* y = z->parent->parent->left;
            if (y != NULL && y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rightRotate(tree, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                leftRotate(tree, z->parent->parent);
            }
        }
    }
    tree->root->color = BLACK;
}

void insertNode(RedBlackTree* tree, int data) {
    Node* z = createNode(data, RED, NULL, NULL, NULL);
    Node* y = NULL;
    Node* x = tree->root;

    while (x != NULL) {
        y = x;
        if (z->data < x->data) {
            x = x->left;
        } else {
            x = x->right;
        }
    }

    z->parent = y;
    if (y == NULL) {
        tree->root = z;
    } else if (z->data < y->data) {
        y->left = z;
    } else {
        y->right = z;
    }

    insertFixup(tree, z);
}

void printTree(Node* root, int level) {
    if (root != NULL) {
        printTree(root->right, level + 1);
        for (int i = 0; i < level; i++) {
            printf("   ");
        }
        printf("%d(%s)\n", root->data, (root->color == RED) ? "RED" : "BLACK");
        printTree(root->left, level + 1);
    }
}

void inOrder(Node* root) {
    if (root != NULL) {
        inOrder(root->left);
        printf("%d ", root->data);
        inOrder(root->right);
    }
}

void reverseInOrder(Node* root) {
    if (root != NULL) {
        reverseInOrder(root->right);
        printf("%d ", root->data);
        reverseInOrder(root->left);
    }
}

// Fonction pour trouver le nœud minimum à partir d'un nœud donné
Node* minimum(Node* node) {
    while (node->left != NULL) {
        node = node->left;
    }
    return node;
}

// Remplacement d'un nœud par un autre
void replaceNode(RedBlackTree* tree, Node* u, Node* v) {
    if (u->parent == NULL) {
        tree->root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    if (v != NULL) {
        v->parent = u->parent;
    }
}

void deleteFixup(RedBlackTree* tree, Node* x) {
    while (x != tree->root && (x == NULL || x->color == BLACK)) {
        if (x == x->parent->left) {
            Node* w = x->parent->right;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                leftRotate(tree, x->parent);
                w = x->parent->right;
            }
            if ((w->left == NULL || w->left->color == BLACK) &&
                (w->right == NULL || w->right->color == BLACK)) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->right == NULL || w->right->color == BLACK) {
                    if (w->left != NULL) w->left->color = BLACK;
                    w->color = RED;
                    rightRotate(tree, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                if (w->right != NULL) w->right->color = BLACK;
                leftRotate(tree, x->parent);
                x = tree->root;
            }
        } else {
            Node* w = x->parent->left;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                rightRotate(tree, x->parent);
                w = x->parent->left;
            }
            if ((w->right == NULL || w->right->color == BLACK) &&
                (w->left == NULL || w->left->color == BLACK)) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->left == NULL || w->left->color == BLACK) {
                    if (w->right != NULL) w->right->color = BLACK;
                    w->color = RED;
                    leftRotate(tree, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                if (w->left != NULL) w->left->color = BLACK;
                rightRotate(tree, x->parent);
                x = tree->root;
            }
        }
    }
    if (x != NULL) {
        x->color = BLACK;
    }
}

void deleteNode(RedBlackTree* tree, int data) {
    Node* z = tree->root;
    while (z != NULL && z->data != data) {
        if (data < z->data) {
            z = z->left;
        } else {
            z = z->right;
        }
    }

    if (z == NULL) {
        printf("Nœud avec la valeur %d introuvable.\n", data);
        return;
    }

    Node* y = z;
    Node* x = NULL;
    Color originalColor = y->color;

    if (z->left == NULL) {
        x = z->right;
        replaceNode(tree, z, z->right);
    } else if (z->right == NULL) {
        x = z->left;
        replaceNode(tree, z, z->left);
    } else {
        y = minimum(z->right);
        originalColor = y->color;
        x = y->right;
        if (y->parent == z) {
            if (x != NULL) x->parent = y;
        } else {
            replaceNode(tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        replaceNode(tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    free(z);

    if (originalColor == BLACK && x != NULL) {
        deleteFixup(tree, x);
    }
}

int main() {
    RedBlackTree tree = {NULL};

    // Insertion de nœuds dans l'arbre
    insertNode(&tree, 10);
    insertNode(&tree, 20);
    insertNode(&tree, 30);
    insertNode(&tree, 40);
    insertNode(&tree, 50);
    insertNode(&tree, 60);
    insertNode(&tree, 102);
    insertNode(&tree, 23);
    insertNode(&tree, 33);
    insertNode(&tree, 65);
    insertNode(&tree, 51);
    insertNode(&tree, 32);
    insertNode(&tree, 1);
    insertNode(&tree, 24);
    insertNode(&tree, 320);
    insertNode(&tree, 140);
    insertNode(&tree, 450);
    insertNode(&tree, 260);
    insertNode(&tree, 420);
    insertNode(&tree, 630);
    insertNode(&tree, 540);
    insertNode(&tree, 250);
    insertNode(&tree, 860);
    insertNode(&tree, 4102);
    insertNode(&tree, 923);
    insertNode(&tree, 833);
    insertNode(&tree, 765);
    insertNode(&tree, 651);
    insertNode(&tree, 532);
    insertNode(&tree, 11);
    insertNode(&tree, 624);
    insertNode(&tree, 1320);
    insertNode(&tree, 1430);
    insertNode(&tree, 451);
    insertNode(&tree, 2620);
    printf("Arbre après insertion des nœuds:\n");
    printTree(tree.root, 0);

    printf("\nListing croissant des éléments de l'arbre:\n");
    inOrder(tree.root);
    printf("\n");

    printf("\nListing décroissant des éléments de l'arbre:\n");
    reverseInOrder(tree.root);
    printf("\n");

    printf("\nSuppression du nœud 30:\n");
    deleteNode(&tree, 30);
    printTree(tree.root, 0);

    printf("\nListing croissant des éléments de l'arbre:\n");
    reverseInOrder(tree.root);
    printf("\n");

    return 0;
}
