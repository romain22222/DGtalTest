#include <utility>

#include "DGtal/base/Common.h"
#include "DGtal/geometry/meshes/NormalCycleComputer.h"
#include "DGtal/helpers/ShortcutsGeometry.h"
#include "DGtal/io/writers/SurfaceMeshWriter.h"
#include "DGtal/io/colormaps/GradientColorMap.h"
#include "DGtal/io/colormaps/QuantifiedColorMap.h"
#include "DGtal/shapes/SurfaceMeshHelper.h"

#include "polyscope/polyscope.h"
#include "polyscope/surface_mesh.h"

using namespace DGtal;
typedef SurfaceMesh< Z3i::RealPoint, Z3i::RealVector > SM;
typedef NormalCycleComputer< Z3i::RealPoint, Z3i::RealVector > NC;
typedef SurfaceMeshHelper< Z3i::RealPoint, Z3i::RealVector > SMH;
typedef SurfaceMeshWriter< Z3i::RealPoint, Z3i::RealVector > SMW;
typedef Shortcuts<Z3i::KSpace>         SH3;
typedef ShortcutsGeometry<Z3i::KSpace> SHG3;
typedef polyscope::SurfaceMesh PolyMesh;
#define R 0.5

#define MAX_STEPS_L2_MEASURE 2

GradientColorMap<double> makeColorMap(double minv, double maxv) {
    GradientColorMap<double> gcm(minv, maxv);
    gcm.addColor(Color(0,0,255));
    gcm.addColor(Color(0,255,255));
    gcm.addColor(Color(255,255,255));
    gcm.addColor(Color(255,255,255));
    gcm.addColor(Color(255,0,255));
    return gcm;
}

void testDGtal() {
    DGtal::trace.info() << "Helloworld from DGtal ";
    DGtal::trace.emphase() << "(version "<< DGTAL_VERSION << ")"<< std::endl;
}

void paper2022Checks() {
    SM smesh = SMH::makeTorus(3.0, 1.0, Z3i::RealPoint(), 20, 20, 0, SMH::NormalsType::VERTEX_NORMALS);
    NC nc(smesh);
    auto mu0 = nc.computeMu0();
    auto mu1 = nc.computeMu1();
    auto mu2 = nc.computeMu2();

    std::vector<double> H(smesh.nbFaces());
    std::vector<double> G(smesh.nbFaces());
    for (auto f = 0; f < smesh.nbFaces(); ++f) {
        const auto b = smesh.faceCentroid(f);
        const auto area = mu0.measure(b, R, f);
        H[f] = NC::meanCurvature(area, mu1.measure(b, R, f));
        G[f] = NC::GaussianCurvature(area, mu2.measure(b, R, f));
    }

    auto mmm = std::minmax_element(H.begin(), H.end());
    auto mmg = std::minmax_element(G.begin(), G.end());

    trace.info() << "Expected mean curvatures: min=0.25 max=0.625\n"
                << "Computed mean curvatures: min=" << *mmm.first << " max=" << *mmm.second << "\n"
                << "Expected Gaussian curvatures: min=-0.5 max=0.25\n"
                << "Computed mean curvatures: min=" << *mmg.first << " max=" << *mmg.second << "\n";

    trace.info() << "\n\n";


    auto muXY = nc.computeMuXY();

    std::vector<double> K1(smesh.nbFaces());
    std::vector<double> K2(smesh.nbFaces());
    std::vector<Z3i::RealVector> D1(smesh.nbFaces());
    std::vector<Z3i::RealVector> D2(smesh.nbFaces());
    smesh.computeFaceNormalsFromPositions();
    for (auto f = 0; f < smesh.nbFaces(); ++f) {
        const auto b = smesh.faceCentroid(f);
        const auto N = smesh.faceNormals()[f];
        const auto area = mu0.measure(b,R,f);
        const auto M = muXY.measure(b,R,f);
        std::tie(K1[f], K2[f], D1[f], D2[f]) = NC::principalCurvatures(area, M, N);
    }

    auto mmk1 = std::minmax_element(K1.begin(), K1.end());
    auto mmk2 = std::minmax_element(K2.begin(), K2.end());

    trace.info() << "Expected k1 curvatures: min=-0.5 max=0.25\n"
                << "Computed k1 curvatures: min="<< *mmk1.first <<" max="<< *mmk1.second <<"\n"
                << "Expected k2 curvatures: min=1 max=1\n"
                << "Computed k2 curvatures: min="<< *mmk2.first <<" max="<< *mmk2.second <<"\n";

    const auto colormapH = makeQuantifiedColorMap(makeColorMap(-0.625, 0.625));
    const auto colormapG = makeQuantifiedColorMap(makeColorMap(-0.625, 0.625));
    auto colorsH = SMW::Colors(smesh.nbFaces());
    auto colorsG = SMW::Colors(smesh.nbFaces());
    for ( auto i = 0; i < smesh.nbFaces(); i++) {
        colorsH[i] = colormapH(H[i]);
        colorsG[i] = colormapG(G[i]);
    }

    SMW::writeOBJ("ex-nc-H", smesh, colorsH);
    SMW::writeOBJ("ex-nc-G", smesh, colorsG);

}

PolyMesh* registerSurface(const CountedPtr<SH3::DigitalSurface>& surface, std::string name) {
    auto primalSurface = SH3::makePrimalSurfaceMesh(surface);
    std::vector<std::vector<size_t>> faces;
    std::vector<Z3i::RealPoint> positions;

    for (auto f = 0; f < primalSurface->nbFaces(); ++f) {
        faces.push_back(primalSurface->incidentVertices(f));
    }
    positions = primalSurface->positions();
    return polyscope::registerSurfaceMesh(std::move(name), positions, faces);
}

PolyMesh* registerDual(const CountedPtr<SH3::DigitalSurface>& surface, std::string name) {

    auto dualSurface = SH3::makeDualPolygonalSurface(surface);

    std::vector<std::vector<size_t>> faces;

    for (auto f = 0; f < dualSurface->nbFaces(); ++f) {
        faces.push_back(dualSurface->verticesAroundFace(f));
    }
    auto positions = dualSurface->positions();
    return polyscope::registerSurfaceMesh(std::move(name), positions, faces);
}

class Varifold {
public:
    Varifold(double l2measure, const Z3i::RealVector& dirPlaneX, const Z3i::RealVector& dirPlaneY)
        : l2measure(l2measure), dirPlaneX(dirPlaneX), dirPlaneY(dirPlaneY) {}
    double l2measure;
    Z3i::RealVector dirPlaneX;
    Z3i::RealVector dirPlaneY;
};

double computeL2Measure(const CountedPtr<SH3::SurfaceMesh>& mesh, int f) {
//    // 1 - Create a queue of faces to process, each element is a pair of face index and the previous step + 1
//    std::queue<std::pair<unsigned long, int>> queue;
//    queue.emplace(f, 0);
//    // 2 - Create a set of visited faces
//    std::set<unsigned long> visited;
//    visited.insert(f);
//    // On each step, transfer one face from the queue to the visited set, and add its neighbors to the queue
//    double l2measure = 0;
//    while (!queue.empty()) {
//        auto top = queue.front();
//        queue.pop();
//        l2measure += 1;
//        if (top.second < MAX_STEPS_L2_MEASURE) {
//            for (auto neighbor : mesh->neighborFaces(top.first)) {
//                if (visited.find(neighbor) == visited.end()) {
//                    queue.emplace(neighbor, top.second + 1);
//                    visited.insert(neighbor);
//                }
//            }
//        }
//    }
//    return 1./l2measure;
    return 1.;
}

std::pair<Z3i::RealVector, Z3i::RealVector> computeDirectionalPlane(const CountedPtr<SH3::SurfaceMesh>& mesh, const NC nc, int f) {
    auto N = mesh->faceNormal(f);
    auto b = mesh->faceCentroid(f);
    auto area = nc.computeMu0().measure(b, R, f);
    auto M = nc.computeMuXY().measure(b, R, f);
    Z3i::RealVector d1, d2;
    double k1, k2;
    std::tie(k1, k2, d1, d2) = NC::principalCurvatures(area, M, N);
    return std::make_pair(d1, d2);
}

std::vector<Varifold> computeVarifolds(const CountedPtr<SH3::SurfaceMesh>& pSurface) {
    std::vector<Varifold> varifolds;


    NC nc(*pSurface);
    pSurface->computeFaceNormalsFromPositions();
    int percent = 0;
    for (auto f = 0; f < pSurface->nbFaces(); ++f) {
        if(f*100/pSurface->nbFaces() > percent) {
            percent += 1;
            DGtal::trace.info() << "Computing varifolds: " << percent << "%\n";
        }
        const auto l2measure = computeL2Measure(pSurface, f);
        const auto dirPlane = computeDirectionalPlane(pSurface, nc, f);
        varifolds.emplace_back(l2measure, dirPlane.first, dirPlane.second);
    }

    return varifolds;
}


int main(int argc, char** argv)
{
    polyscope::init();

    auto params = SH3::defaultParameters() | SHG3::defaultParameters();
    std::string filename = "../DGtalObjects/bunny33.vol";
    auto binImage = SH3::makeBinaryImage(filename, params);
    auto K = SH3::getKSpace(binImage);
    auto surface = SH3::makeDigitalSurface(binImage, K, params);
    auto primalSurface = SH3::makePrimalSurfaceMesh(surface);
    auto polyBunny = registerSurface(surface, "bunny");
    // Create the dual surface mesh and register it
//    /*auto polyDualBunny =*/ registerDual(surface, "dual bunny");

    // Compute a heat map of the L2 measure
    DGtal::trace.info() << primalSurface->nbFaces() << std::endl;
    auto varifolds = computeVarifolds(primalSurface);
    DGtal::trace.info() << "Computed " << varifolds.size() << " varifolds" << std::endl;
//    auto minmax = std::minmax_element(varifolds.begin(), varifolds.end(), [](const Varifold& a, const Varifold& b) {
//        return a.l2measure < b.l2measure;
//    });
//
//    const auto colormap = makeColorMap(minmax.first->l2measure, minmax.second->l2measure);
//    DGtal::trace.info() << "Min L2: " << minmax.first->l2measure << " Max L2: " << minmax.second->l2measure << std::endl;
//    std::vector<std::vector<double>> colorMeasure;
//    for (auto i = 0; i < primalSurface->nbFaces(); i++) {
//        const auto color = colormap(varifolds[i].l2measure);
//        colorMeasure.push_back({static_cast<double>(color.red())/255, static_cast<double>(color.green())/255, static_cast<double>(color.blue())/255});
//    }
//
//    polyBunny->addFaceColorQuantity("L2 Measure", colorMeasure);

    // Create 2 vector fields on the surface, one for each direction of the principal curvatures
    std::vector<Z3i::RealVector> dir1;
    std::vector<Z3i::RealVector> dir2;
    for (auto i = 0; i < primalSurface->nbFaces(); i++) {
        dir1.push_back(varifolds[i].dirPlaneX);
        dir2.push_back(varifolds[i].dirPlaneY);
    }
    polyBunny->addFaceVectorQuantity("Principal Curvature 1", dir1);
    polyBunny->addFaceVectorQuantity("Principal Curvature 2", dir2);

    polyscope::show();
    return 0;
}