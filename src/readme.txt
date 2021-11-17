// Copyright (C) 2021 Cocioran Stefan 321 CA

Pentru realizarea temei m-am folosit de scheletul laboratorului 8 pe care l-am 
adaptat la cerintele enuntului temei.

- subscriber -

Se creeaza o conexiune cu serverul, se trimite ID-ul clientului si se dezactiveaza 
algoritmul lui Nagle. Clientul poate primi de la tastatura comenzile "subscribe", 
"unsubscribe" si "exit". Daca primeste o comanda invalida, se afiseaza o eroare 
corespunzatoare. Atunci cand primeste un mesaj de la server, acesta contine toate 
campurile de care are nevoie pentru compunerea si afisarea unui mesaj conform cerintei.


- server -

Pentru a tine evidenta clientilor care sunt conectati la server si a datelor care 
trebuiesc trimise catre acestia am folosit "unordered_map" (deoarece este 
implementat folosind "hash table", costul pentru cautare, insertie si stergere 
este O(1), fiind mai eficient decat un simplu "map").

client_messages      - retine mesajele pentru abonamentele cu SF primite cat timp 
                       clientul era offline, in ordinea in care au fost primite
client_sock          - retine ce socket ii este atribuit conexiunii cu fiecare client
client_online        - retine starea clientilor, online/offline
client_subscriptions - retine topicurile la care este abonat un client si SF-ul acestora

Se creeaza un socket pentru conexiunile UDP si un socket pasiv pe care se 
primesc cereri de la clientii TCP. Atunci cand se primeste o cerere de conexiune, 
se verifica daca clientul respectiv este deja conectat. Daca nu este conectat, 
se seteaza starea acestuia ca fiind online si se trimit mesajele stocate pentru 
abonamentele cu SF.

Cand se primeste un mesaj de la clientii UDP, este convertit in formatul cerut 
in enunt, apoi se verifica pentru fiecare client daca este abonat la topicul 
pentru care s-au primit informatii. Daca este abonat si este online, se trimite 
mesajul direct. Daca este abonat si nu este online, mesajul va fi stocat doar 
daca abonamentul pentru topicul respectiv este unul cu SF. 

Cand se primesc date de la clientii TCP, verific daca conexiunea s-a inchis si 
setez starea clientului ca fiind offline, se inchide socketul si nu mai este 
atribuit ID-ului clientului respectiv. Daca nu s-a inchis conexiunea, inseamna 
ca am primit una dintre comenzile "subscribe" sau "unsubscribe" si se actualizeaza 
abonamentele clientilor pentru acel topic. 

Daca serverul primeste de la tastatura mesajul "exit", atunci se inchide atat 
serverul, cat si conexiunea cu toti clientii.