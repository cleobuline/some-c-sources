# Some C Sources
Une collection de programmes en C par Cleobuline.

## forth_gmp.c
Un interpréteur Forth moderne avec GMP, créé par Cleobuline ( avec l'aide de Grok xAI).

### Fonctionnalités
- Arithmétique illimitée (GMP)
- Pile : `DUP`, `SWAP`, `OVER`, `ROT`, `DROP`, `PICK`, `ROLL`
- Variables : `VARIABLE`, `@`, `!`, `+!`
- Contrôle : `IF`, `DO LOOP`, `CASE`
- Bitwise : `&`, `|`, `^`, `~`, `LSHIFT`, `RSHIFT`
- Gestion : `WORDS`, `FORGET`, `LOAD`

### Compilation
```bash
gcc -o forth_gmp forth_gmp.c -lgmp
