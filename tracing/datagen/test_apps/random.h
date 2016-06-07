#ifndef random_h__
#define random_h__

extern void init(void);
extern double uniform(void);
extern double uniformDouble(double, double);
extern int uniformInt(int, int);
extern double expDouble(double);
extern int expInt(double);
extern int choice(double *, int);
extern void pdfToCdf(double *, double *, int);

#endif // foo_h__
