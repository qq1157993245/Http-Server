#pragma once

#include <stdio.h>
#include <stdbool.h>
#include "rwlock.h"

typedef struct file_lock file_lock;

file_lock* create_file_lock(void);

void delete_file_lock(file_lock**);

bool insert_to_file_lock(file_lock*, char*, rwlock_t*);

bool remove_from_file_lock(file_lock*, char*);

bool look_for_item(file_lock*, char*, rwlock_t**);

