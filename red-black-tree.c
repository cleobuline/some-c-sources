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
    Node* nil;   /* Sentinel NIL — nœud noir partagé pour toutes les feuilles */
} RedBlackTree;

/* ─────────────────────────────────────────────────────────────────────────────
 * Initialisation de l'arbre avec le sentinel NIL.
 * Toutes les feuilles pointent vers nil (noir), ce qui élimine les
 * vérifications != NULL dans les rotations et le fixup.
 * ───────────────────────────────────────────────────────────────────────────*/
void initTree(RedBlackTree* tree) {
    tree->nil = (Node*)malloc(sizeof(Node));
    if (!tree->nil) { perror("malloc nil"); exit(EXIT_FAILURE); }
    tree->nil->color  = BLACK;
    tree->nil->parent = tree->nil;
    tree->nil->left   = tree->nil;
    tree->nil->right  = tree->nil;
    tree->nil->data   = 0;
    tree->root = tree->nil;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Libération récursive de tous les nœuds (hors sentinel).
 * ───────────────────────────────────────────────────────────────────────────*/
static void freeNodes(RedBlackTree* tree, Node* node) {
    if (node == tree->nil) return;
    freeNodes(tree, node->left);
    freeNodes(tree, node->right);
    free(node);
}

void freeTree(RedBlackTree* tree) {
    freeNodes(tree, tree->root);
    free(tree->nil);
    tree->root = NULL;
    tree->nil  = NULL;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Création d'un nœud — les feuilles pointent vers nil.
 * ───────────────────────────────────────────────────────────────────────────*/
Node* createNode(RedBlackTree* tree, int data) {
    Node* n = (Node*)malloc(sizeof(Node));
    if (!n) { perror("malloc node"); exit(EXIT_FAILURE); }
    n->data   = data;
    n->color  = RED;
    n->parent = tree->nil;
    n->left   = tree->nil;
    n->right  = tree->nil;
    return n;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Rotations — nil étant un vrai nœud, plus aucun test != NULL nécessaire.
 * ───────────────────────────────────────────────────────────────────────────*/
void leftRotate(RedBlackTree* tree, Node* x) {
    Node* y  = x->right;
    x->right = y->left;
    if (y->left != tree->nil)
        y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == tree->nil)
        tree->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left   = x;
    x->parent = y;
}

void rightRotate(RedBlackTree* tree, Node* y) {
    Node* x  = y->left;
    y->left  = x->right;
    if (x->right != tree->nil)
        x->right->parent = y;
    x->parent = y->parent;
    if (y->parent == tree->nil)
        tree->root = x;
    else if (y == y->parent->left)
        y->parent->left = x;
    else
        y->parent->right = x;
    x->right  = y;
    y->parent = x;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * insertFixup — restaure les propriétés RB après insertion.
 * BUG 6 fix : z->parent->parent ne peut plus être nil car la boucle while
 * garantit que z->parent est rouge, et la racine est toujours noire.
 * ───────────────────────────────────────────────────────────────────────────*/
void insertFixup(RedBlackTree* tree, Node* z) {
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            Node* y = z->parent->parent->right;
            if (y->color == RED) {
                /* Cas 1 : l'oncle est rouge → recoloration */
                z->parent->color          = BLACK;
                y->color                  = BLACK;
                z->parent->parent->color  = RED;
                z = z->parent->parent;
            } else {
                /* Cas 2 : z est enfant droit → rotation gauche */
                if (z == z->parent->right) {
                    z = z->parent;
                    leftRotate(tree, z);
                }
                /* Cas 3 : z est enfant gauche → rotation droite */
                z->parent->color         = BLACK;
                z->parent->parent->color = RED;
                rightRotate(tree, z->parent->parent);
            }
        } else {
            /* Symétrique */
            Node* y = z->parent->parent->left;
            if (y->color == RED) {
                z->parent->color         = BLACK;
                y->color                 = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rightRotate(tree, z);
                }
                z->parent->color         = BLACK;
                z->parent->parent->color = RED;
                leftRotate(tree, z->parent->parent);
            }
        }
    }
    tree->root->color = BLACK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * insertNode — BUG 5 fix : doublons refusés avec message.
 * ───────────────────────────────────────────────────────────────────────────*/
void insertNode(RedBlackTree* tree, int data) {
    Node* z = createNode(tree, data);
    Node* y = tree->nil;
    Node* x = tree->root;

    while (x != tree->nil) {
        y = x;
        if (z->data < x->data)
            x = x->left;
        else if (z->data > x->data)
            x = x->right;
        else {
            /* BUG 5 fix : doublon détecté */
            printf("Valeur %d déjà présente, insertion ignorée.\n", data);
            free(z);
            return;
        }
    }

    z->parent = y;
    if (y == tree->nil)
        tree->root = z;
    else if (z->data < y->data)
        y->left = z;
    else
        y->right = z;

    insertFixup(tree, z);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * minimum — utilise nil comme sentinelle.
 * ───────────────────────────────────────────────────────────────────────────*/
Node* minimum(RedBlackTree* tree, Node* node) {
    while (node->left != tree->nil)
        node = node->left;
    return node;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * replaceNode (transplant) — inchangé logiquement, nil gère les cas NULL.
 * ───────────────────────────────────────────────────────────────────────────*/
void replaceNode(RedBlackTree* tree, Node* u, Node* v) {
    if (u->parent == tree->nil)
        tree->root = v;
    else if (u == u->parent->left)
        u->parent->left = v;
    else
        u->parent->right = v;
    v->parent = u->parent;  /* Toujours valide car v != NULL grâce à nil */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * deleteFixup — BUG 1+3 fix : x ne peut plus être NULL (c'est nil à la place),
 * et w ne peut plus être NULL non plus. La boucle while est simplifiée.
 * ───────────────────────────────────────────────────────────────────────────*/
void deleteFixup(RedBlackTree* tree, Node* x) {
    while (x != tree->root && x->color == BLACK) {
        if (x == x->parent->left) {
            Node* w = x->parent->right;
            if (w->color == RED) {
                /* Cas 1 */
                w->color          = BLACK;
                x->parent->color  = RED;
                leftRotate(tree, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {
                /* Cas 2 */
                w->color = RED;
                x = x->parent;
            } else {
                if (w->right->color == BLACK) {
                    /* Cas 3 */
                    w->left->color = BLACK;
                    w->color       = RED;
                    rightRotate(tree, w);
                    w = x->parent->right;
                }
                /* Cas 4 */
                w->color          = x->parent->color;
                x->parent->color  = BLACK;
                w->right->color   = BLACK;
                leftRotate(tree, x->parent);
                x = tree->root;
            }
        } else {
            /* Symétrique */
            Node* w = x->parent->left;
            if (w->color == RED) {
                w->color         = BLACK;
                x->parent->color = RED;
                rightRotate(tree, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == BLACK && w->left->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->left->color == BLACK) {
                    w->right->color = BLACK;
                    w->color        = RED;
                    leftRotate(tree, w);
                    w = x->parent->left;
                }
                w->color         = x->parent->color;
                x->parent->color = BLACK;
                w->left->color   = BLACK;
                rightRotate(tree, x->parent);
                x = tree->root;
            }
        }
    }
    x->color = BLACK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * deleteNode — BUG 2 fix : deleteFixup appelé même si x == nil (nœud noir
 * feuille supprimé), car nil est un vrai nœud avec un parent valide.
 * ───────────────────────────────────────────────────────────────────────────*/
void deleteNode(RedBlackTree* tree, int data) {
    /* Recherche du nœud */
    Node* z = tree->root;
    while (z != tree->nil && z->data != data) {
        if (data < z->data) z = z->left;
        else                z = z->right;
    }
    if (z == tree->nil) {
        printf("Nœud avec la valeur %d introuvable.\n", data);
        return;
    }

    Node* y             = z;
    Node* x             = tree->nil;
    Color originalColor = y->color;

    if (z->left == tree->nil) {
        x = z->right;
        replaceNode(tree, z, z->right);
    } else if (z->right == tree->nil) {
        x = z->left;
        replaceNode(tree, z, z->left);
    } else {
        y             = minimum(tree, z->right);
        originalColor = y->color;
        x             = y->right;
        if (y->parent == z) {
            x->parent = y;          /* Même si x == nil, nil->parent = y */
        } else {
            replaceNode(tree, y, y->right);
            y->right         = z->right;
            y->right->parent = y;
        }
        replaceNode(tree, z, y);
        y->left         = z->left;
        y->left->parent = y;
        y->color        = z->color;
    }

    free(z);

    /* BUG 2 fix : on appelle toujours deleteFixup si la couleur originale
     * était noire — même si x == nil, c'est un nœud valide avec parent. */
    if (originalColor == BLACK)
        deleteFixup(tree, x);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Affichage
 * ───────────────────────────────────────────────────────────────────────────*/
void printTree(RedBlackTree* tree, Node* root, int level) {
    if (root != tree->nil) {
        printTree(tree, root->right, level + 1);
        for (int i = 0; i < level; i++) printf("   ");
        printf("%d(%s)\n", root->data, root->color == RED ? "RED" : "BLACK");
        printTree(tree, root->left, level + 1);
    }
}

void inOrder(RedBlackTree* tree, Node* root) {
    if (root != tree->nil) {
        inOrder(tree, root->left);
        printf("%d ", root->data);
        inOrder(tree, root->right);
    }
}

void reverseInOrder(RedBlackTree* tree, Node* root) {
    if (root != tree->nil) {
        reverseInOrder(tree, root->right);
        printf("%d ", root->data);
        reverseInOrder(tree, root->left);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────────*/
int main(void) {
    RedBlackTree tree;
    initTree(&tree);

    int values[] = {
        10, 20, 30, 40, 50, 60, 102, 23, 33, 65, 51, 32, 1, 24,
        320, 140, 450, 260, 420, 630, 540, 250, 860, 4102, 923,
        833, 765, 651, 532, 11, 624, 1320, 1430, 451, 2620
    };
    int n = sizeof(values) / sizeof(values[0]);
    for (int i = 0; i < n; i++)
        insertNode(&tree, values[i]);

    printf("Arbre après insertion des nœuds:\n");
    printTree(&tree, tree.root, 0);

    printf("\nListing croissant:\n");
    inOrder(&tree, tree.root);
    printf("\n");

    printf("\nListing décroissant:\n");
    reverseInOrder(&tree, tree.root);
    printf("\n");

    printf("\nSuppression du nœud 30:\n");
    deleteNode(&tree, 30);
    printTree(&tree, tree.root, 0);

    printf("\nListing décroissant après suppression:\n");
    reverseInOrder(&tree, tree.root);
    printf("\n");

    /* BUG 4 fix : libération complète de la mémoire */
    freeTree(&tree);
    printf("\nMémoire libérée proprement.\n");

    return 0;
}
