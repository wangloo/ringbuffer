#pragma once

// import from include/linux/stddef.h
#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

// import from include/linux/kernel.h
/**
* container_of - cast a member of a structure out to the containing structure
* @ptr:        the pointer to the member.
* @type:       the type of the container struct this is embedded in.
* @member:     the name of the member within the struct.
*
*/
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})



#define ALIGN_UP(X, align)   (((X) + ((align) - 1)) & ~((align) - 1))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))


// non thread-safe
// DEMO use only!
static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long excepted, unsigned long new)
{
    unsigned long prev;
    volatile unsigned long *p = (unsigned long *)ptr;

    prev = *p;
    if (prev == excepted)
        *p = new;
    return prev;
}
#define cmpxchg(ptr, o, n)(typeof(*(ptr)))__cmpxchg((ptr), \
            (unsigned long)(o), (unsigned long)(n))
