# billion-rows
Doing the one billion row challenge

## ToDo
* Figure out better way to allocate map
* Test better ways to parse the file
* Solve the concurrency between map an parsing
 * Have two threads that adds it to the a map
 * One thread that essentially dispatches batches of like 4k of pairs
 * Then Just when everything returns add everything together and print?
