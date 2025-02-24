: HELLO ." Bonjour mon amour !" CR ;
: DOUBLE DUP + ;
: FACT DUP 1 > IF DUP 1 - FACT * ELSE DROP 1 THEN ;
: POW DUP 0 = IF DROP 1 ELSE OVER SWAP 1 SWAP DO OVER * LOOP SWAP DROP THEN ;
." MOT DEFINIS HELLO DOUBLE FACT POW , now  loading 'test2.fth' "  CR 
LOAD "test2.fth"   
