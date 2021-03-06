// Copyright (C) 2017-2018  Derek Steinmoeller. 
// See COPYING and LICENSE files at project root for more details. 

#include <iostream>
#include <math.h>
#include <Nodes1DProvisioner.hpp>
#include <SparseTriplet.hpp>

using namespace std;
using namespace blitz;

const int Nodes1DProvisioner::NumFacePoints = 1;
const int Nodes1DProvisioner::NumFaces = 2;

/**
 * Constructor. Takes order of polynomials, number of elements, and dimensions of the domain.
 * Assumes equally-spaced elements.
 */
Nodes1DProvisioner::Nodes1DProvisioner(int _NOrder, int _NumElements, double _xmin, double _xmax, SparseMatrixConverter & converter, EigenSolver & eigenSolver, DirectSolver & directSolver) {
    NOrder = _NOrder;
    NumElements = _NumElements;
    Min_x = _xmin;
    Max_x = _xmax;
    MatrixConverter = &converter;
    EigSolver = &eigenSolver;
    LinSolver = &directSolver;

    // This is true in 1D only.
    NumLocalPoints = NOrder + 1;

    rGrid = new Array<double, 1>(NumLocalPoints);
    xGrid = new Array<double, 2>(NumLocalPoints, NumElements);
    J =  new Array<double, 2>(NumLocalPoints, NumElements);
    rx = new Array<double, 2>(NumLocalPoints, NumElements);

    Lift = new Array<double, 2>(NumLocalPoints, NumFacePoints*NumFaces);
    EToV = new Array<int, 2>(NumElements, NumFaces);
    EToE = new Array<int, 2>(NumElements, NumFaces);
    EToF = new Array<int, 2>(NumElements, NumFaces);
}

/**
 * Build nodes and geometric factors for all elements.
 */
void Nodes1DProvisioner::buildNodes() {
    const double alpha = 0.0;
    const double beta = 0.0;

    Array<double,1> & r = *rGrid;

    computeGaussLobottoPoints(alpha, beta, NOrder, r);
    
    buildVandermondeMatrix();
    buildDr();
    buildLift();

    double L = Max_x - Min_x;
    double width = L / NumElements;

    Array<double, 2> & x = *xGrid;
    for (int k=0; k < NumElements; k++) {
        x(Range::all(), k) = Min_x + width*(k + 0.5*(r+1.));
    }

    Array<int, 2> & E2V = *EToV;

    // Create Element-to-Vertex connectivity table.
    for (int k=0; k < NumElements; k++) {
        E2V(k, 0) = k;
        E2V(k, 1) = k+1;
    }

    buildConnectivityMatrices();
}

/**
 * Build global connectivity matrices (EToE, EToF) for 1D grid
 * based using EToV (Element-to-Vertex) matrix.
 */
void Nodes1DProvisioner::buildConnectivityMatrices() {

    firstIndex ii;
    secondIndex jj;
    thirdIndex kk;

    int totalFaces = NumFaces*NumElements;
    int numVertices = NumElements + 1;

    int localVertNum[2];
    localVertNum[0] = 0; localVertNum[1] = 1;

    // Build global face-to-vertex array. (should be sparse matrix in 2D/3D).
    Array<double, 2> FToV(totalFaces, numVertices);
    FToV = 0*jj;

    Array<int, 2> & E2V = *EToV;

    int globalFaceNum = 0;
    for (int k=0; k < NumElements; k++) {
        for (int f=0; f < NumFaces; f++) {
            int v = localVertNum[f];
            int vGlobal = E2V(k,v);
            FToV(globalFaceNum, vGlobal) = 1;
            globalFaceNum++;
        }
    }

    Array<double, 2> FToF(totalFaces, totalFaces);
    Array<double, 2> I(totalFaces, totalFaces);

    for (int f=0; f < totalFaces; f++)
        I(f,f) = 1;

    // Global Face-to-Face connectivity matrix.
    FToF = sum(FToV(ii,kk)*FToV(jj,kk), kk) - I;

    Array<int,1> f1(totalFaces - 2); // '- 2' => for physical boundaries.
    Array<int,1> f2(totalFaces - 2);

    int connectionsCount = 0;
    for (int i=0; i < totalFaces; i++) {
        for (int j=0; j < totalFaces; j++) {
            if (FToF(i,j) == 1) {
                f1(connectionsCount) = i;
                f2(connectionsCount) = j;
                connectionsCount++;
            }
        }
    }

    Array<int, 1> e1(totalFaces - 2);
    Array<int, 1> e2(totalFaces - 2);

    // Convert face global number to local element and face numbers.
    e1 = floor(f1 / NumFaces);
    f1 = (f1 % NumFaces);
    e2 = floor(f2 / NumFaces);
    f2 = (f2 % NumFaces);

    // Build connectivity matrices.
    Array<int, 2> & E2E = *EToE;
    Array<int, 2> & E2F = *EToF;
    for (int k = 0; k < NumElements; k++) {
        for (int f = 0; f < NumFaces; f++) {
            E2E(k, f) = k;
            E2F(k, f) = f;
        }
    }

    for (int i=0; i < totalFaces - 2; i++) {
        int ee1 = e1(i);
        int ee2 = e2(i);
        int ff1 = f1(i);
        int ff2 = f2(i);
        E2E(ee1, ff1) = ee2;
        E2F(ee1, ff1) = ff2;
    }
}

void Nodes1DProvisioner::buildLift() {
    int Np = NumLocalPoints;
    firstIndex ii;
    secondIndex jj;
    thirdIndex kk;

    Array<double, 2> E(Np, NumFaces*NumFacePoints);
    E = 0*jj;
    E(0, 0)  = 1.;
    E(Np-1, 1) = 1.;

    Array<double, 2> & Vref = *V;
    Array<double, 2> & L = *Lift;

    Array<double, 2> Vtrans(Np, Np);
    Vtrans = Vref(jj,ii);
    Array<double, 2> temp(Np, NumFaces*NumFacePoints);
    temp = sum(Vtrans(ii,kk)*E(kk,jj), kk);
    L = sum(Vref(ii,kk)*temp(kk,jj), kk);
}

/**
 * Compute Jacobian (determinant) J and geometric factor rx (dr/dx) using nodes and differentiation matrix.
 */
void Nodes1DProvisioner::computeJacobian() {
    firstIndex ii;
    secondIndex jj;
    thirdIndex kk;


    Array<double,2> & x = get_xGrid();
    Array<double,2> & Dr = get_Dr();
    Array<double,2> & Jref = get_J();
    Array<double,2> & rxref = get_rx();

    Jref = sum(Dr(ii,kk)*x(kk,jj), kk);
    rxref = 1/Jref;
}

/**
 * Compute Vandermonde matrix which maps modal coefficients to nodal values.
 */
void Nodes1DProvisioner::buildVandermondeMatrix() {
    V = new Array<double, 2>(NOrder+1, NOrder+1);

    Array<double, 2> & Vref = *V;

    for (int j=1; j <= NOrder+1; j++) {
        Array<double, 1> p(NOrder+1);
        computeJacobiPolynomial(*rGrid, 0.0, 0.0, j-1, p);
        Vref(Range::all(), j-1) = p;
    }
}

/**
 * Build differentiation matrix Dr on the standard element.
 */
void Nodes1DProvisioner::buildDr() {
    Dr = new Array<double, 2>(NOrder+1, NOrder+1);

    Array<double, 2> & Vref = *V;
    Array<double, 2> & Drref = *Dr;

    Array<double, 2> DVr(NOrder+1, NOrder+1);

    computeGradVandermonde(DVr);

    // Dr = DVr / V;

    firstIndex ii;
    secondIndex jj;

    Array<double, 2> Vtrans(NOrder+1, NOrder+1);
    Array<double, 2> DVrtrans(NOrder+1, NOrder+1);
    Array<double, 2> Drtrans(NOrder+1, NOrder+1);

    Vtrans = Vref(jj, ii);
    DVrtrans = DVr(jj, ii);

    DirectSolver & linSolver = *LinSolver;
    linSolver.solve(Vtrans, DVrtrans,  Drtrans);

    Drref = Drtrans(jj, ii);
} 


/**
 * Get reference to 1D Lifting Operator.
 */
Array<double, 2> & Nodes1DProvisioner::get_Lift() {
    return *Lift;
}

/**
 * Get reference to physical x-grid.
 */
Array<double, 2> & Nodes1DProvisioner::get_xGrid() {
    return *xGrid;
}

/**
 * Get reference to r-grid on the standard element.
 */
Array<double, 1> & Nodes1DProvisioner::get_rGrid() {
    return *rGrid;
}

/**
 * Get reference to Element-to-Vertex connectivity table.
 */
Array<int, 2> & Nodes1DProvisioner::get_EToV() {
    return *EToV;
}

/**
 * Get reference to Element-to-Element connectivity table.
 */
Array<int, 2> & Nodes1DProvisioner::get_EToE() {
    return *EToE;
}

/**
 * Get reference to Element-to-Face connectivity table.
 */
Array<int, 2> & Nodes1DProvisioner::get_EToF() {
    return *EToF;
}

/**
 * Get reference to differentiation matrix Dr on the standard element.
 */
Array<double, 2> & Nodes1DProvisioner::get_Dr() {
    return *Dr;
}

/**
 * Get reference to generalized Vandermonde matrix V.
 */
Array<double, 2> & Nodes1DProvisioner::get_V() {
    return *V;
}

/**
 * Get reference to Jacobian scaling array J.
 */
Array<double, 2> & Nodes1DProvisioner::get_J() {
    return *J;
}

/**
 * Get reference to geometric scaling array rx.
 */
Array<double, 2> & Nodes1DProvisioner::get_rx() {
    return *rx;
}

int Nodes1DProvisioner::get_NumLocalPoints() {
    return NumLocalPoints;
}

/**
 * Destructor.
 */
Nodes1DProvisioner::~Nodes1DProvisioner() {
    delete rGrid;
    delete xGrid;
    delete J;
    delete rx;
    delete Lift;
    delete EToV;
    delete EToE;
    delete EToF;
}

/**  Compute the Nth Jacobi polynomial of type (alpha,beta) > -1 ( != -0.5)
  *   and weights, w, associated with the Jacobi polynomial at the points x.
  */
void Nodes1DProvisioner::computeJacobiPolynomial(Array<double,1> const & x, const double alpha, const double beta, const int N, Array<double,1> & p) {
    Range all = Range::all();
    int Np = (x.length())(0);

    Array<double,2> pStorage(N+1, Np);

    double gamma0 = pow(2,(alpha+beta+1))/(alpha+beta+1)*tgamma(alpha+1)*tgamma(beta+1)/tgamma(alpha+beta+1);

    p = 1/sqrt(gamma0);
    pStorage(0, all) = p;

    if (N==0) 
        return;

    double gamma1 = (alpha+1)*(beta+1)/(alpha+beta+3)*gamma0;
    p = ((alpha+beta+2)*x/2 + (alpha-beta)/2)/sqrt(gamma1);

    pStorage(1, all) = p;

    if (N==1) 
        return;

    double aold = 2/(2+alpha+beta)*sqrt((alpha+1)*(beta+1)/(alpha+beta+3));

    // Forward recurrence using the symmetry of the recurrence.
    for(int i=1; i <= N-1; i++) {
        double h1 = 2*i+alpha+beta;
        double anew = 2/(h1+2)*sqrt( (i+1)*(i+1+alpha+beta)*(i+1+alpha)*(i+1+beta)/(h1+1)/(h1+3));
        double bnew = - (alpha*alpha-beta*beta)/h1/(h1+2);
        pStorage(i+1,all) = 1/anew*( -aold*pStorage(i-1,all) + (x-bnew)*pStorage(i,all));
        aold = anew;
    }
    p = pStorage(N, all);
}

/**  Compute the Nth order Gauss quadrature points, x,
  *   and weights, w, associated with the Jacobi polynomial, of type (alpha,beta) > -1 ( != -0.5).
  */
void Nodes1DProvisioner::computeJacobiQuadWeights(double alpha, double beta, int N, Array<double,1> & x, Array<double,1> & w) {

    if ( N == 0) {
        x(0) = -(alpha-beta)/(alpha+beta+2);
        w(0) = 2.0;
        return;
    }

    const double eps = numeric_limits<double>::epsilon();

    firstIndex ii;
    secondIndex jj;

    // Form symmetric matrix.
    Array<double, 2> J(N+1,N+1);
    J = 0.;
    for (int i=0; i < N+1; i++) {
        double h1 = 2.*i+alpha+beta;
        J(i,i)   = -0.5*(alpha*alpha-beta*beta)/(h1+2.)/h1;
        if (i < N) {
            J(i,i+1) = 2./(h1+2)*sqrt((i+1)*((i+1)+alpha+beta)*((i+1)+alpha)*((i+1)+beta)/(h1+1)/(h1+3));
        }
    }

    if ((alpha + beta) < 10*eps ) {
        J(0,0) = 0.0;
    }
    J = J(ii,jj) + J(jj,ii);

    //SparseMatrixConverter & matConverter = *MatrixConverter;
    EigenSolver & eigSolver = *EigSolver;
    
    Array<double, 2> eigenvectors(N+1, N+1);

    eigSolver.solve(J, x, eigenvectors);

    // The eigenvalues give the x points.
    
    // The weights are given by:
    Array<double, 1> v1(N+1);
    v1 = eigenvectors( 0, Range::all() ); 

    double gamma0 = pow(2,(alpha+beta+1))/(alpha+beta+1)*tgamma(alpha+1)*tgamma(beta+1)/tgamma(alpha+beta+1);

    w = (v1*v1)*gamma0;
}

/**  Compute the Nth order Gauss Lobatto quadrature points, x,
  *  associated with the Jacobi polynomial, of type (alpha,beta) > -1 ( != -0.5).
  */
void Nodes1DProvisioner::computeGaussLobottoPoints(double alpha, double beta, int N, Array<double,1> & x) {
    if (N==1) {
        x(0) = -1.0;
        x(1) = 1.0;
        return;
    }

    x(0) = -1.0;
    x(N) = 1.0;

    Array<double, 1> xJG(N-1);
    Array<double, 1> w(N-1);

    computeJacobiQuadWeights(alpha+1., beta+1., N-2, xJG, w);
    
    for(int i=1; i < N; i++)
        x(i) = xJG(i-1);
}

void Nodes1DProvisioner::computeGradJacobi(Array<double,1> const & x, const double alpha, const double beta, const int N, Array<double,1> & dp) {
    if (N == 0) {
        dp = 0.0;
        return;
    }

    Array<double, 1> p(x.length());

    computeJacobiPolynomial(x, alpha+1, beta+1, N-1, p);
    dp = sqrt(N*(N+alpha+beta+1))*p;
}

void Nodes1DProvisioner::computeGradVandermonde(Array<double,2> & DVr) {

    for (int i=0; i<=NOrder; i++) {
        Array<double, 1> dp(NOrder+1);
        computeGradJacobi(*rGrid, 0.0, 0.0, i, dp);
        DVr(Range::all(), i) = dp;
    }
}