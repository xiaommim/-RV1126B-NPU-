#ifndef SYSROOT_FIX_H
#define SYSROOT_FIX_H

/* * 专门用于抹平 GCC 8.3 与 Debian 11/12 高版本 glibc 之间的宏定义代差
 * 将老编译器不认识的新属性全部定义为空，骗过编译器
 */
#define __attr_dealloc_free
#define __attr_dealloc(arg1, arg2)
#define __attribute_alloc_align__(arg)
#define __attr_access(arg)
#define __fortified_attr_access(a, b, c)

#endif // SYSROOT_FIX_H
