
#ifndef MW_PROXY_API_H
#define MW_PROXY_API_H

#ifdef MW_PROXY_STATIC_DEFINE
#  define MW_PROXY_API
#  define MW_PROXY_NO_EXPORT
#else
#  ifndef MW_PROXY_API
#    ifdef mw_proxy_EXPORTS
        /* We are building this library */
#      define MW_PROXY_API __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define MW_PROXY_API __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef MW_PROXY_NO_EXPORT
#    define MW_PROXY_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef MW_PROXY_DEPRECATED
#  define MW_PROXY_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef MW_PROXY_DEPRECATED_EXPORT
#  define MW_PROXY_DEPRECATED_EXPORT MW_PROXY_API MW_PROXY_DEPRECATED
#endif

#ifndef MW_PROXY_DEPRECATED_NO_EXPORT
#  define MW_PROXY_DEPRECATED_NO_EXPORT MW_PROXY_NO_EXPORT MW_PROXY_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef MW_PROXY_NO_DEPRECATED
#    define MW_PROXY_NO_DEPRECATED
#  endif
#endif

#endif /* MW_PROXY_API_H */
