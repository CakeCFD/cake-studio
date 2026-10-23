#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>

struct Vec3 { double x, y, z; };

// Eq.17: smoothness indicator using simple finite difference gradient
// For a stencil of cells, beta = |grad u|^2 averaged (first order)
// + curvature term (second order)
double beta(const std::vector<double>& vals, const std::vector<Vec3>& offs)
{
    int n = vals.size();
    if (n < 2) return 0.0;

    // Use central cell (index 0) as origin
    double sumW = 0.0, gradX = 0.0, gradY = 0.0, gradZ = 0.0;
    double curv = 0.0;

    for (int i = 1; i < n; ++i) {
        double dx = offs[i].x - offs[0].x;
        double dy = offs[i].y - offs[0].y;
        double dz = offs[i].z - offs[0].z;
        double h2 = dx*dx + dy*dy + dz*dz + 1e-30;
        double du = vals[i] - vals[0];
        double w  = 1.0 / h2;

        // Directional gradient: project du onto direction
        gradX += w * (du * dx / h2);
        gradY += w * (du * dy / h2);
        gradZ += w * (du * dz / h2);
        sumW  += w;

        // Curvature: second difference (variation from linear prediction)
        // curv += w * sqr(du); // raw
    }

    if (sumW > 0) { gradX /= sumW; gradY /= sumW; gradZ /= sumW; }

    // First-order term: |grad|^2
    double beta1 = gradX*gradX + gradY*gradY + gradZ*gradZ;

    // Second-order term: residual variance after linear removal
    double beta2 = 0.0;
    for (int i = 1; i < n; ++i) {
        double dx = offs[i].x - offs[0].x;
        double dy = offs[i].y - offs[0].y;
        double dz = offs[i].z - offs[0].z;
        double predicted = vals[0] + gradX*dx + gradY*dy + gradZ*dz;
        double res = vals[i] - predicted;
        beta2 += res*res;
    }
    beta2 /= (n - 1);

    return beta1 + beta2;
}

double gamma_k(double beta_k, double eps=1e-12, int q=6)
{
    return 1.0 / std::pow(beta_k + eps, q);
}

struct TenoWeights {
    double chiK, chi0, chi1;
    double deltaK, delta0, delta1;
};

TenoWeights tenoSelect(double betaK, double beta0, double beta1, double C_T=1e-6)
{
    double gK = gamma_k(betaK), g0 = gamma_k(beta0), g1 = gamma_k(beta1);
    double gSum = gK + g0 + g1;
    TenoWeights w;
    w.chiK = gK/gSum; w.chi0 = g0/gSum; w.chi1 = g1/gSum;
    w.deltaK = (w.chiK < C_T) ? 0.0 : 1.0;
    w.delta0 = (w.chi0 < C_T) ? 0.0 : 1.0;
    w.delta1 = (w.chi1 < C_T) ? 0.0 : 1.0;
    return w;
}

int main()
{
    std::cout << "=== TENO Scheme Validation (Fu et al. 2021) ===\n\n";

    // Test 1: smooth sin field
    {
        std::vector<double> valsK = {0.0, 0.588, 0.951, 0.588, 0.0};
        std::vector<Vec3>   offsK = {{0,0,0},{0.2,0,0},{0.4,0,0},{-0.2,0,0},{-0.4,0,0}};
        std::vector<double> vals0 = {0.0, 0.588, 0.951};
        std::vector<Vec3>   offs0 = {{0,0,0},{0.2,0,0},{0.4,0,0}};
        std::vector<double> vals1 = {0.951, 0.588, 0.0};
        std::vector<Vec3>   offs1 = {{0,0,0},{0.2,0,0},{0.4,0,0}};

        double bK=beta(valsK,offsK), b0=beta(vals0,offs0), b1=beta(vals1,offs1);
        auto w = tenoSelect(bK, b0, b1);
        std::cout << "SMOOTH REGION:\n";
        std::cout << "  betaK=" << bK << " beta0=" << b0 << " beta1=" << b1 << "\n";
        std::cout << "  chiK=" << w.chiK << " chi0=" << w.chi0 << " chi1=" << w.chi1 << "\n";
        std::cout << "  deltaK=" << w.deltaK << " -> large stencil: " << (w.deltaK>0.5?"YES (correct)":"NO") << "\n\n";
    }

    // Test 2: step discontinuity at face
    {
        std::vector<double> valsK = {1.0, 1.0, 0.0, 0.0, 1.0};
        std::vector<Vec3>   offsK = {{0,0,0},{-0.2,0,0},{0.2,0,0},{0.4,0,0},{-0.4,0,0}};
        std::vector<double> vals0 = {1.0, 1.0, 1.0};  // left side, smooth
        std::vector<Vec3>   offs0 = {{0,0,0},{-0.2,0,0},{-0.4,0,0}};
        std::vector<double> vals1 = {0.0, 0.0, 0.0};  // right side, smooth
        std::vector<Vec3>   offs1 = {{0,0,0},{0.2,0,0},{0.4,0,0}};

        double bK=beta(valsK,offsK), b0=beta(vals0,offs0), b1=beta(vals1,offs1);
        auto w = tenoSelect(bK, b0, b1);
        std::cout << "DISCONTINUITY:\n";
        std::cout << "  betaK=" << bK << " beta0=" << b0 << " beta1=" << b1 << "\n";
        std::cout << "  chiK=" << w.chiK << " chi0=" << w.chi0 << " chi1=" << w.chi1 << "\n";
        std::cout << "  deltaK=" << w.deltaK << " -> large stencil rejected: " << (w.deltaK<0.5?"YES (correct)":"NO (bad!)") << "\n";
        std::cout << "  delta0=" << w.delta0 << " delta1=" << w.delta1 << " -> ENO among small stencils\n\n";
    }

    // Test 3: gradient field (smooth but high gradient)
    {
        std::vector<double> valsK = {0.0, 0.1, 0.2, 0.3, 0.4};
        std::vector<Vec3>   offsK = {{0,0,0},{0.1,0,0},{0.2,0,0},{0.3,0,0},{0.4,0,0}};
        std::vector<double> vals0 = {0.0, 0.1, 0.2};
        std::vector<Vec3>   offs0 = {{0,0,0},{0.1,0,0},{0.2,0,0}};
        std::vector<double> vals1 = {0.2, 0.3, 0.4};
        std::vector<Vec3>   offs1 = {{0,0,0},{0.1,0,0},{0.2,0,0}};

        double bK=beta(valsK,offsK), b0=beta(vals0,offs0), b1=beta(vals1,offs1);
        auto w = tenoSelect(bK, b0, b1);
        std::cout << "LINEAR GRADIENT (smooth):\n";
        std::cout << "  betaK=" << bK << " beta0=" << b0 << " beta1=" << b1 << "\n";
        std::cout << "  chiK=" << w.chiK << " chi0=" << w.chi0 << " chi1=" << w.chi1 << "\n";
        std::cout << "  deltaK=" << w.deltaK << " -> large stencil: " << (w.deltaK>0.5?"YES (correct)":"NO") << "\n\n";
    }

    return 0;
}
