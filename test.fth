: HELLO ." Bonjour mon amour !" CR ;
: DOUBLE DUP + ;
: FACT DUP 1 > IF DUP 1 - FACT * ELSE DROP 1 THEN ;
." MOT DEFINIS HELLO DOUBLE FACT loading 'test2.fth' "  CR 
LOAD "test2.fth"   
