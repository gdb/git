#ifndef FLAVORTEXT_H
#define FLAVORTEXT_H

void flavortext_init();
void flavortext_insert(const char *key, const char *val);
const char *flavortext_lookup(const char *key);

#endif /* FLAVORTEXT_H */
