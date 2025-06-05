/*
MIT License

Copyright (c) 2021 Parallel Applications Modelling Group - GMAP 
	GMAP website: https://gmap.pucrs.br
	
	Pontifical Catholic University of Rio Grande do Sul (PUCRS)
	Av. Ipiranga, 6681, Porto Alegre - Brazil, 90619-900

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

------------------------------------------------------------------------------

The original NPB 3.4.1 version was written in Fortran and belongs to: 
	http://www.nas.nasa.gov/Software/NPB/

Authors of the Fortran code:
	M. Yarrow
	C. Kuszmaul
	H. Jin

------------------------------------------------------------------------------

The serial C++ version is a translation of the original NPB 3.4.1
Serial C++ version: https://github.com/GMAP/NPB-CPP/tree/master/NPB-SER

Authors of the C++ code: 
	Dalvan Griebler <dalvangriebler@gmail.com>
	Gabriell Araujo <hexenoften@gmail.com>
 	Júnior Löff <loffjh@gmail.com>

------------------------------------------------------------------------------

The OpenMP version is a parallel implementation of the serial C++ version
OpenMP version: https://github.com/GMAP/NPB-CPP/tree/master/NPB-OMP

Authors of the OpenMP code:
	Júnior Löff <loffjh@gmail.com>
	
*/

#include "omp.h"
#include "../common/npb-CPP.hpp"
#include "npbparams.hpp"

#define T_INIT 0
#define T_BENCH 1
#define T_CONJ_GRAD 2
#define T_LAST 3

struct ProblemConfig {
    int na_val;
    int nonzer_val;
    int niter_val;
    double shift_val;
    double rcond_val;
    double zeta_verify_value;
};

static ProblemConfig getProblemConfig(char class_npb) {
    ProblemConfig config;
    config.rcond_val = 0.1;
    
    switch(class_npb) {
        case 'S':
        case 's':
            config.na_val = 1400;
            config.nonzer_val = 7;
            config.niter_val = 15;
            config.shift_val = 10.0;
            config.zeta_verify_value = 8.5971775078648;
            break;
        case 'W':
        case 'w':
            config.na_val = 7000;
            config.nonzer_val = 8;
            config.niter_val = 15;
            config.shift_val = 12.0;
            config.zeta_verify_value = 10.362595087124;
            break;
        case 'A':
        case 'a':
            config.na_val = 14000;
            config.nonzer_val = 11;
            config.niter_val = 15;
            config.shift_val = 20.0;
            config.zeta_verify_value = 17.130235054029;
            break;
        case 'B':
        case 'b':
            config.na_val = 75000;
            config.nonzer_val = 13;
            config.niter_val = 75;
            config.shift_val = 60.0;
            config.zeta_verify_value = 22.712745482631;
            break;
        case 'C':
        case 'c':
            config.na_val = 150000;
            config.nonzer_val = 15;
            config.niter_val = 75;
            config.shift_val = 110.0;
            config.zeta_verify_value = 28.973605592845;
            break;
        case 'D':
        case 'd':
            config.na_val = 1500000;
            config.nonzer_val = 21;
            config.niter_val = 100;
            config.shift_val = 500.0;
            config.zeta_verify_value = 52.514532105794;
            break;
        case 'E':
        case 'e':
            config.na_val = 9000000;
            config.nonzer_val = 26;
            config.niter_val = 100;
            config.shift_val = 1500.0;
            config.zeta_verify_value = 77.522164599383;
            break;
        default:
            printf("Unknown class: %c\n", class_npb);
            printf("Available classes: S, W, A, B, C, D, E\n");
            exit(EXIT_FAILURE);
    }
    return config;
}

static int *colidx;
static int *rowstr;
static int *iv;
static int *arow;
static int *acol;
static double *aelt;
static double *a;
static double *x;
static double *z;
static double *p;
static double *q;
static double *r;

static int naa;
static int nzz;
static int firstrow;
static int lastrow;
static int firstcol;
static int lastcol;
static double amult;
static double tran;
static boolean timeron;

static ProblemConfig config;

static void conj_grad(int colidx[],
		int rowstr[],
		double x[],
		double z[],
		double a[],
		double p[],
		double q[],
		double r[],
		double* rnorm);
static int icnvrt(double x,
		int ipwr2);
static void makea(int n,
		int nz,
		double a[],
		int colidx[],
		int rowstr[],
		int firstrow,
		int lastrow,
		int firstcol,
		int lastcol,
		int arow[],
		int acol[],
		double aelt[],
		int iv[]);
static void sparse(double a[],
		int colidx[],
		int rowstr[],
		int n,
		int nz,
		int nozer,
		int arow[],
		int acol[],
		double aelt[],
		int firstrow,
		int lastrow,
		int nzloc[],
		double rcond,
		double shift);
static void sprnvc(int n,
		int nz,
		int nn1,
		double v[],
		int iv[]);
static void vecset(int n,
		double v[],
		int iv[],
		int* nzv,
		int i,
		double val);

int main(int argc, char **argv){
    if(argc != 3) {
        printf("Usage: %s <class> <num_threads>\n", argv[0]);
        printf("Classes: S, W, A, B, C, D, E\n");
        printf("Example: %s B 4\n", argv[0]);
        return 1;
    }
    
    char class_npb = argv[1][0];
    int num_threads = atoi(argv[2]);
    
    if(num_threads <= 0) {
        printf("Number of threads must be positive\n");
        return 1;
    }
    
    config = getProblemConfig(class_npb);
    
    int NZ = config.na_val * (config.nonzer_val + 1) * (config.nonzer_val + 1);
    int NAZ = config.na_val * (config.nonzer_val + 1);
    
    colidx = (int*)malloc(sizeof(int) * NZ);
    rowstr = (int*)malloc(sizeof(int) * (config.na_val + 1));
    iv = (int*)malloc(sizeof(int) * config.na_val);
    arow = (int*)malloc(sizeof(int) * config.na_val);
    acol = (int*)malloc(sizeof(int) * NAZ);
    aelt = (double*)malloc(sizeof(double) * NAZ);
    a = (double*)malloc(sizeof(double) * NZ);
    x = (double*)malloc(sizeof(double) * (config.na_val + 2));
    z = (double*)malloc(sizeof(double) * (config.na_val + 2));
    p = (double*)malloc(sizeof(double) * (config.na_val + 2));
    q = (double*)malloc(sizeof(double) * (config.na_val + 2));
    r = (double*)malloc(sizeof(double) * (config.na_val + 2));
    
    if(!colidx || !rowstr || !iv || !arow || !acol || !aelt || !a || !x || !z || !p || !q || !r) {
        printf("Memory allocation failed\n");
        return 1;
    }
    
    omp_set_num_threads(num_threads);
    
	int	i, j, k, it;
	double zeta;
	double rnorm;
	double norm_temp1, norm_temp2;
	double t, mflops, tmax;
	boolean verified;
	double epsilon, err;
	char *t_names[T_LAST];

	for(i=0; i<T_LAST; i++){
		timer_clear(i);
	}

	FILE* fp;
	if((fp = fopen("timer.flag", "r")) != NULL){
		timeron = TRUE;
		t_names[T_INIT] = (char*)"init";
		t_names[T_BENCH] = (char*)"benchmk";
		t_names[T_CONJ_GRAD] = (char*)"conjgd";
		fclose(fp);
	}else{
		timeron = FALSE;
	}

	timer_start(T_INIT);

	firstrow = 0;
	lastrow  = config.na_val-1;
	firstcol = 0;
	lastcol  = config.na_val-1;

	printf("\n\n NAS Parallel Benchmarks 4.1 Parallel C++ version with OpenMP - CG Benchmark\n\n");
	printf(" Size: %11d\n", config.na_val);
	printf(" Iterations: %5d\n", config.niter_val);
	printf(" Class: %c\n", class_npb);
	printf(" Number of threads: %d\n", num_threads);

	naa = config.na_val;
	nzz = NZ;

	tran    = 314159265.0;
	amult   = 1220703125.0;
	zeta    = randlc( &tran, amult );

	makea(naa, 
			nzz, 
			a, 
			colidx, 
			rowstr, 
			firstrow, 
			lastrow, 
			firstcol, 
			lastcol, 
			arow, 
			acol, 
			aelt,
			iv);

	#pragma omp parallel private(it,i,j,k)	
	{
		#pragma omp for nowait
		for(j = 0; j < lastrow - firstrow + 1; j++){
			for(k = rowstr[j]; k < rowstr[j+1]; k++){
				colidx[k] = colidx[k] - firstcol;
			}
		}

		#pragma omp for nowait
		for(i = 0; i < config.na_val+1; i++){
			x[i] = 1.0;
		}
		#pragma omp for nowait
		for(j = 0; j<lastcol-firstcol+1; j++){
			q[j] = 0.0;
			z[j] = 0.0;
			r[j] = 0.0;
			p[j] = 0.0;
		}
		
		#pragma omp single
			zeta = 0.0;

		for(it = 1; it <= 1; it++){
			conj_grad(colidx, rowstr, x, z, a, p, q, r, &rnorm);
			#pragma omp single
			{
				norm_temp1 = 0.0;
				norm_temp2 = 0.0;
			}
			
			#pragma omp for reduction(+:norm_temp1,norm_temp2)
			for(j = 0; j < lastcol - firstcol + 1; j++){
				norm_temp1 += x[j] * z[j];
				norm_temp2 += + z[j] * z[j];
			}

			#pragma omp single
				norm_temp2 = 1.0 / sqrt(norm_temp2);

			#pragma omp for
			for(j = 0; j < lastcol - firstcol + 1; j++){     
				x[j] = norm_temp2 * z[j];
			}

		}

		#pragma omp for
		for(i = 0; i < config.na_val+1; i++){
			x[i] = 1.0;
		}

		#pragma omp single
			zeta = 0.0;

		#pragma omp master
		{
			timer_stop(T_INIT);

			printf(" Initialization time = %15.3f seconds\n", timer_read(T_INIT));
			
			timer_start(T_BENCH);
		}

		for(it = 1; it <= config.niter_val; it++){
			
			#pragma omp master
			if(timeron){timer_start(T_CONJ_GRAD);}
			conj_grad(colidx, rowstr, x, z, a, p, q, r, &rnorm);
			#pragma omp master
			if(timeron){timer_stop(T_CONJ_GRAD);}

			#pragma omp single
			{
				norm_temp1 = 0.0;
				norm_temp2 = 0.0;
			}

			#pragma omp for reduction(+:norm_temp1,norm_temp2)
			for(j = 0; j < lastcol - firstcol + 1; j++){
				norm_temp1 += x[j]*z[j];
				norm_temp2 += z[j]*z[j];
			}
			#pragma omp single
			{
				norm_temp2 = 1.0 / sqrt(norm_temp2);
				zeta = config.shift_val + 1.0 / norm_temp1;
			}

			#pragma omp master
			{
				if(it==1){printf("\n   iteration           ||r||                 zeta\n");}
				printf("    %5d       %20.14e%20.13e\n", it, rnorm, zeta);
			}
			#pragma omp for 
			for(j = 0; j < lastcol - firstcol + 1; j++){
				x[j] = norm_temp2 * z[j];
			}
		}
	}
	timer_stop(T_BENCH);

	t = timer_read(T_BENCH);

	printf(" Benchmark completed\n");

	epsilon = 1.0e-10;
	if(class_npb != 'U' && class_npb != 'u'){
		err = fabs(zeta - config.zeta_verify_value) / config.zeta_verify_value;
		if(err <= epsilon){
			verified = TRUE;
			printf(" VERIFICATION SUCCESSFUL\n");
			printf(" Zeta is    %20.13e\n", zeta);
			printf(" Error is   %20.13e\n", err);
		}else{
			verified = FALSE;
			printf(" VERIFICATION FAILED\n");
			printf(" Zeta                %20.13e\n", zeta);
			printf(" The correct zeta is %20.13e\n", config.zeta_verify_value);
		}
	}else{
		verified = FALSE;
		printf(" Problem size unknown\n");
		printf(" NO VERIFICATION PERFORMED\n");
	}
	if(t != 0.0){
		mflops = (double)(2.0*config.niter_val*config.na_val)
			* (3.0+(double)(config.nonzer_val*(config.nonzer_val+1))
					+ 25.0
					* (5.0+(double)(config.nonzer_val*(config.nonzer_val+1)))+3.0)
			/ t / 1000000.0;
	}else{
		mflops = 0.0;
	}
	
	char num_threads_str[16];
	sprintf(num_threads_str, "%d", num_threads);
	
	c_print_results((char*)"CG",
			class_npb,
			config.na_val,
			0,
			0,
			config.niter_val,
			t,
			mflops,
			(char*)"          floating point",
			verified,
			(char*)NPBVERSION,
			(char*)COMPILETIME,
			(char*)COMPILERVERSION,
			(char*)LIBVERSION,
			num_threads_str,
			(char*)CS1,
			(char*)CS2,
			(char*)CS3,
			(char*)CS4,
			(char*)CS5,
			(char*)CS6,
			(char*)CS7);

	if(timeron){
		tmax = timer_read(T_BENCH);
		if(tmax == 0.0){tmax = 1.0;}
		printf("  SECTION   Time (secs)\n");
		for(i = 0; i < T_LAST; i++){
			t = timer_read(i);
			if(i == T_INIT){
				printf("  %8s:%9.3f\n", t_names[i], t);
			}else{
				printf("  %8s:%9.3f  (%6.2f%%)\n", t_names[i], t, t*100.0/tmax);
				if(i == T_CONJ_GRAD){
					t = tmax - t;
					printf("    --> %8s:%9.3f  (%6.2f%%)\n", "rest", t, t*100.0/tmax);
				}
			}
		}
	}

	free(colidx);
	free(rowstr);
	free(iv);
	free(arow);
	free(acol);
	free(aelt);
	free(a);
	free(x);
	free(z);
	free(p);
	free(q);
	free(r);

	return 0;
}

static void conj_grad(int colidx[],
		int rowstr[],
		double x[],
		double z[],
		double a[],
		double p[],
		double q[],
		double r[],
		double* rnorm){
	int j, k;
	int cgit, cgitmax;
	double alpha, beta, suml;
	static double d, sum, rho, rho0;

	cgitmax = 25;
	#pragma omp single nowait
	{
		rho = 0.0;
		sum = 0.0;
	}
	#pragma omp for
	for(j = 0; j < naa+1; j++){
		q[j] = 0.0;
		z[j] = 0.0;
		r[j] = x[j];
		p[j] = r[j];
	}
 
	#pragma omp for reduction(+:rho)
	for(j = 0; j < lastcol - firstcol + 1; j++){
		rho += r[j]*r[j];
	}

	for(cgit = 1; cgit <= cgitmax; cgit++){
		#pragma omp single nowait
		{
			d = 0.0;
			rho0 = rho;
			rho = 0.0;
		}

		#pragma omp for nowait
		for(j = 0; j < lastrow - firstrow + 1; j++){
			suml = 0.0;
			for(k = rowstr[j]; k < rowstr[j+1]; k++){
				suml += a[k]*p[colidx[k]];
			}
			q[j] = suml;
		}

		#pragma omp for reduction(+:d)
		for (j = 0; j < lastcol - firstcol + 1; j++) {
			d += p[j]*q[j];
		}

		alpha = rho0 / d;

		#pragma omp for reduction(+:rho)
		for(j = 0; j < lastcol - firstcol + 1; j++){
			z[j] += alpha*p[j];
			r[j] -= alpha*q[j];
			rho += r[j]*r[j];
		}

		beta = rho / rho0;

		#pragma omp for
		for(j = 0; j < lastcol - firstcol + 1; j++){
			p[j] = r[j] + beta*p[j];
		}
	}

	#pragma omp for nowait
	for(j = 0; j < lastrow - firstrow + 1; j++){
		suml = 0.0;
		for(k = rowstr[j]; k < rowstr[j+1]; k++){
			suml += a[k]*z[colidx[k]];
		}
		r[j] = suml;
	}

	#pragma omp for reduction(+:sum)
	for(j = 0; j < lastcol-firstcol+1; j++){
		suml   = x[j] - r[j];
		sum += suml*suml;
	}
	#pragma omp single
		*rnorm = sqrt(sum);
}

static int icnvrt(double x, int ipwr2){
	return (int)(ipwr2 * x);
}

static void makea(int n,
		int nz,
		double a[],
		int colidx[],
		int rowstr[],
		int firstrow,
		int lastrow,
		int firstcol,
		int lastcol,
		int arow[],
		int acol[],
		double aelt[],
		int iv[]){
	int iouter, ivelt, nzv, nn1;
	int *ivc = (int*)malloc(sizeof(int) * (config.nonzer_val + 1));
	double *vc = (double*)malloc(sizeof(double) * (config.nonzer_val + 1));

	nn1 = 1;
	do{
		nn1 = 2 * nn1;
	}while(nn1 < n);

	for(iouter = 0; iouter < n; iouter++){
		nzv = config.nonzer_val;
		sprnvc(n, nzv, nn1, vc, ivc);
		vecset(n, vc, ivc, &nzv, iouter+1, 0.5);
		arow[iouter] = nzv;
		for(ivelt = 0; ivelt < nzv; ivelt++){
			acol[iouter * (config.nonzer_val + 1) + ivelt] = ivc[ivelt] - 1;
			aelt[iouter * (config.nonzer_val + 1) + ivelt] = vc[ivelt];
		}
	}

	sparse(a,
			colidx,
			rowstr,
			n,
			nz,
			config.nonzer_val,
			arow,
			acol,
			aelt,
			firstrow,
			lastrow,
			iv,
			config.rcond_val,
			config.shift_val);
			
	free(ivc);
	free(vc);
}

static void sparse(double a[],
		int colidx[],
		int rowstr[],
		int n,
		int nz,
		int nozer,
		int arow[],
		int acol[],
		double aelt[],
		int firstrow,
		int lastrow,
		int nzloc[],
		double rcond,
		double shift){	
	int nrows;
	int i, j, j1, j2, nza, k, kk, nzrow, jcol;
	double size, scale, ratio, va;
	boolean goto_40;

	nrows = lastrow - firstrow + 1;

	for(j = 0; j < nrows+1; j++){
		rowstr[j] = 0;
	}
	for(i = 0; i < n; i++){
		for(nza = 0; nza < arow[i]; nza++){
			j = acol[i * (config.nonzer_val + 1) + nza] + 1;
			rowstr[j] = rowstr[j] + arow[i];
		}
	}
	rowstr[0] = 0;
	for(j = 1; j < nrows+1; j++){
		rowstr[j] = rowstr[j] + rowstr[j-1];
	}
	nza = rowstr[nrows] - 1;

	if(nza > nz){
		printf("Space for matrix elements exceeded in sparse\n");
		printf("nza, nzmax = %d, %d\n", nza, nz);
		exit(EXIT_FAILURE);
	}

	for(j = 0; j < nrows; j++){
		for(k = rowstr[j]; k < rowstr[j+1]; k++){
			a[k] = 0.0;
			colidx[k] = -1;
		}
		nzloc[j] = 0;
	}

	size = 1.0;
	ratio = pow(rcond, (1.0 / (double)(n)));
	for(i = 0; i < n; i++){
		for(nza = 0; nza < arow[i]; nza++){
			j = acol[i * (config.nonzer_val + 1) + nza];

			scale = size * aelt[i * (config.nonzer_val + 1) + nza];
			for(nzrow = 0; nzrow < arow[i]; nzrow++){
				jcol = acol[i * (config.nonzer_val + 1) + nzrow];
				va = aelt[i * (config.nonzer_val + 1) + nzrow] * scale;

				if(jcol == j && j == i){
					va = va + rcond - shift;
				}

				goto_40 = FALSE;
				for(k = rowstr[j]; k < rowstr[j+1]; k++){
					if(colidx[k] > jcol){
						for(kk = rowstr[j+1]-2; kk >= k; kk--){
							if(colidx[kk] > -1){
								a[kk+1] = a[kk];
								colidx[kk+1] = colidx[kk];
							}
						}
						colidx[k] = jcol;
						a[k]  = 0.0;
						goto_40 = TRUE;
						break;
					}else if(colidx[k] == -1){
						colidx[k] = jcol;
						goto_40 = TRUE;
						break;
					}else if(colidx[k] == jcol){
						nzloc[j] = nzloc[j] + 1;
						goto_40 = TRUE;
						break;
					}
				}
				if(goto_40 == FALSE){
					printf("internal error in sparse: i=%d\n", i);
					exit(EXIT_FAILURE);
				}
				a[k] = a[k] + va;
			}
		}
		size = size * ratio;
	}

	for(j = 1; j < nrows; j++){
		nzloc[j] = nzloc[j] + nzloc[j-1];
	}

	for(j = 0; j < nrows; j++){
		if(j > 0){
			j1 = rowstr[j] - nzloc[j-1];
		}else{
			j1 = 0;
		}
		j2 = rowstr[j+1] - nzloc[j];
		nza = rowstr[j];
		for(k = j1; k < j2; k++){
			a[k] = a[nza];
			colidx[k] = colidx[nza];
			nza = nza + 1;
		}
	}
	for(j = 1; j < nrows+1; j++){
		rowstr[j] = rowstr[j] - nzloc[j-1];
	}
	nza = rowstr[nrows] - 1;
}

static void sprnvc(int n, int nz, int nn1, double v[], int iv[]){
	int nzv, ii, i;
	double vecelt, vecloc;

	nzv = 0;

	while(nzv < nz){
		vecelt = randlc(&tran, amult);
		vecloc = randlc(&tran, amult);
		i = icnvrt(vecloc, nn1) + 1;
		if(i>n){continue;}

		boolean was_gen = FALSE;
		for(ii = 0; ii < nzv; ii++){
			if(iv[ii] == i){
				was_gen = TRUE;
				break;
			}
		}
		if(was_gen){continue;}
		v[nzv] = vecelt;
		iv[nzv] = i;
		nzv = nzv + 1;
	}
}

static void vecset(int n, double v[], int iv[], int* nzv, int i, double val){
	int k;
	boolean set;

	set = FALSE;
	for(k = 0; k < *nzv; k++){
		if(iv[k] == i){
			v[k] = val;
			set  = TRUE;
		}
	}
	if(set == FALSE){
		v[*nzv]  = val;
		iv[*nzv] = i;
		*nzv     = *nzv + 1;
	}
}