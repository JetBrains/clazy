#include <QtCore/QList>
#include <QtCore/QVector>




struct A {
    int v;
};
Q_DECLARE_TYPEINFO(A, Q_PRIMITIVE_TYPE);



struct B {
    int v;
};


struct C {
    int v;
};
Q_DECLARE_TYPEINFO(C, Q_MOVABLE_TYPE);

void test()
{
    QList<A> l1;
    QList<B> l2; // Warning
    QVector<A> l3;
    QVector<B> l4; // Warning

    QList<C> l5;
    QVector<C> l6;
}
