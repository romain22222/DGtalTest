#include <utility>

#include "DGtal/base/Common.h"
#include "DGtal/helpers/ShortcutsGeometry.h"
#include "DGtal/io/writers/SurfaceMeshWriter.h"
#include "DGtal/io/colormaps/GradientColorMap.h"
#include "DGtal/io/colormaps/QuantifiedColorMap.h"
#include "DGtal/shapes/SurfaceMeshHelper.h"

#include "polyscope/polyscope.h"
#include "polyscope/surface_mesh.h"

using namespace DGtal;
using namespace DGtal::Z3i;
typedef Shortcuts<KSpace>         SH3;
typedef ShortcutsGeometry<KSpace> SHG3;
typedef polyscope::SurfaceMesh PolyMesh;

std::pair<GradientColorMap<double>, GradientColorMap<double>> makeColorMap(double minv, double maxv) {
    if (maxv < 0) {
        GradientColorMap<double> gcm(minv, maxv);
        gcm.addColor(Color(0,0,255));
        gcm.addColor(Color(255,255,255));
        return {gcm, gcm};
    }
    if (minv > 0) {
        GradientColorMap<double> gcm(minv, maxv);
        gcm.addColor(Color(255,255,255));
        gcm.addColor(Color(255,0,0));
        gcm.addColor(Color(0,0,0));
        return {gcm, gcm};
    }
    GradientColorMap<double> gcm(minv, 0.);
    gcm.addColor(Color(0,0,255));
    gcm.addColor(Color(255,255,255));
    GradientColorMap<double> gcm2(0., maxv);
    gcm2.addColor(Color(255,255,255));
    gcm2.addColor(Color(255,0,0));
    gcm2.addColor(Color(0,0,0));
    return {gcm, gcm2};
}

PolyMesh* registerSurface(const SH3::SurfaceMesh& surface, std::string name) {
    std::vector<std::vector<size_t>> faces;
    std::vector<RealPoint> positions;
    auto smP = CountedPtr<SH3::SurfaceMesh>(new SH3::SurfaceMesh(surface));

    for (auto f = 0; f < surface.nbFaces(); ++f) {
        faces.push_back(surface.incidentVertices(f));
    }
    positions = surface.positions();
    return polyscope::registerSurfaceMesh(std::move(name), positions, faces);
}

class Varifold {
public:
    Varifold(const RealPoint& position, const RealVector& planeNormal, const RealVector& curvature)
        : position(position), planeNormal(planeNormal), curvature(curvature) {
    }

    RealPoint position;
    RealVector planeNormal;
    RealVector curvature;
};

typedef enum {
    FlatDisc,
    Cone,
    HalfSphere
} DistributionType;

class RadialDistance {
public:
    RadialDistance(): center(0,0,0), radius(1) {};
    RadialDistance(const RealPoint& center, const double radius, const DistributionType& distribution)
        : center(center), radius(radius) {
        switch (distribution) {
            case DistributionType::FlatDisc:
                measureFunction = [](double dRatio) {
                    return 3./(4*M_PI);
                };
                measureFunctionDerivate = [](double dRatio) {
                    return 0;
                };
                break;
            case DistributionType::Cone:
                measureFunction = [](double dRatio) {
                    return (1-dRatio) * M_PI/12.;
                };
                measureFunctionDerivate = [](double dRatio) {
                    return -M_PI/12.;
                };
                break;
            case DistributionType::HalfSphere:
                measureFunction = [](double dRatio) {
                    return (1-dRatio*dRatio)/(M_PI * 2);
                };
                measureFunctionDerivate = [](double dRatio) {
                    return -dRatio/(M_PI);
                };
                break;
        }
    }
    RealPoint center;
    double radius;
    std::function<double(double)> measureFunction;
    std::function<double(double)> measureFunctionDerivate;

    std::vector<std::pair<double,double>> operator()(const SH3::RealPoints& mesh) const {
        std::vector<std::pair<double,double>> wf;
        for (const auto& b : mesh) {
            // If the face is inside the radius, compute the weight
            const auto d = (b - center).norm();
            if (d < radius) {
                wf.emplace_back(measureFunction(d / radius), measureFunctionDerivate(d / radius));
            } else {
                wf.emplace_back(0., 0.);
            }
        }
        return wf;
    }
};

typedef enum {
    TrivialNormalFaceCentroid,
    DualNormalFaceCentroid,
    CorrectedNormalFaceCentroid,
    ProbabilisticOfTrivials,
    VertexInterpolation
} Method;

RealVector projection(const RealVector& toProject, const RealVector& planeNormal) {
    return toProject - planeNormal * (toProject.dot(planeNormal)/planeNormal.squaredNorm());
}

std::vector<RealVector> computeLocalCurvature(const CountedPtr<SH3::BinaryImage>& bimage, const CountedPtr<SH3::DigitalSurface>& surface, const double cRadius, const DistributionType cDistribType, const Method method) {
    std::vector<RealVector> curvatures;
    const CountedPtr<SH3::SurfaceMesh> pSurface = SH3::makePrimalSurfaceMesh(surface);
    RadialDistance rd;
    std::vector<std::pair<double, double>> weights;
    RealVector tmpSumTop;
    double tmpSumBottom;
    RealVector tmpVector;

    auto positions = SH3::RealPoints();

    auto normals = SH3::RealVectors();

    unsigned long nbElements;

    pSurface->computeFaceNormalsFromPositions();
    pSurface->computeVertexNormalsFromFaceNormals();

    switch (method) {
        case TrivialNormalFaceCentroid:
            nbElements = pSurface->nbFaces();
            for (auto f = 0; f < nbElements; ++f) {
                positions.push_back(pSurface->faceCentroid(f));
                normals.push_back(pSurface->faceNormal(f));
            }
            break;
        case DualNormalFaceCentroid:
            nbElements = pSurface->nbVertices();
            for (auto v = 0; v < nbElements; ++v) {
                positions.push_back(pSurface->position(v));
                normals.push_back(pSurface->vertexNormal(v));
            }
            break;
        case CorrectedNormalFaceCentroid:
            nbElements = pSurface->nbFaces();
            normals = SHG3::getIINormalVectors(bimage, SH3::getSurfelRange(surface), SHG3::defaultParameters()("verbose", 0));
            for (auto f = 0; f < nbElements; ++f) {
                positions.push_back(pSurface->faceCentroid(f));
            }
            break;
        default:
            return curvatures;
    }

    for (auto f = 0; f < nbElements; ++f) {
        tmpSumTop = RealVector();
        tmpSumBottom = 0;
        const auto b = positions[f];
        rd = RadialDistance(b, cRadius, cDistribType);
        weights = rd(positions);
        for (auto otherF = 0; otherF < nbElements; ++otherF) {
            if (weights[otherF].first > 0) {
                if (f != otherF) {
                    tmpVector = positions[otherF] - b;
                    tmpSumTop += weights[otherF].first * projection(tmpVector, normals[otherF])/tmpVector.norm();
                }
                tmpSumBottom += weights[otherF].first;
            }
        }
        curvatures.push_back(-tmpSumTop/(tmpSumBottom*cRadius));
    }

    return curvatures;
}

std::vector<Varifold> computeVarifolds(const CountedPtr<SH3::BinaryImage>& bimage, const CountedPtr<SH3::DigitalSurface>& surface, const double cRadius, const DistributionType cDistribType, const Method method) {
    std::vector<Varifold> varifolds;

    auto ps = *SH3::makePrimalSurfaceMesh(surface);

    auto curvatures = computeLocalCurvature(bimage, surface, cRadius, cDistribType, method);

    SH3::RealVectors normals;

    switch (method) {
        case TrivialNormalFaceCentroid:
        case CorrectedNormalFaceCentroid:
            ps.computeFaceNormalsFromPositions();
            if (method == Method::CorrectedNormalFaceCentroid) {
                normals = SHG3::getIINormalVectors(bimage, SH3::getSurfelRange(surface), SHG3::defaultParameters()("verbose", 0));
            } else {
                ps.computeVertexNormalsFromFaceNormals();
                normals = ps.faceNormals();
            }

            for (auto f = 0; f < ps.nbFaces(); ++f) {
                varifolds.emplace_back(ps.faceCentroid(f), normals[f], curvatures[f]);
            }
            break;
        case DualNormalFaceCentroid:
            ps.computeFaceNormalsFromPositions();
            ps.computeVertexNormalsFromFaceNormals();
            for (auto v = 0; v < ps.nbVertices(); ++v) {
                varifolds.emplace_back(ps.position(v), ps.vertexNormal(v), curvatures[v]);
            }
            break;
        default:
            break;
    }

    return varifolds;
}

DistributionType argToDistribType(const std::string& arg) {
    if (arg == "fd") {
        return DistributionType::FlatDisc;
    } else if (arg == "c") {
        return DistributionType::Cone;
    } else {
        return DistributionType::HalfSphere;
    }
}

Method argToMethod(const std::string& arg) {
    if (arg == "tnfc") {
        return Method::TrivialNormalFaceCentroid;
    } else if (arg == "dnfc") {
        return Method::DualNormalFaceCentroid;
    } else if (arg == "cnfc") {
        return Method::CorrectedNormalFaceCentroid;
    } else if (arg == "pot") {
        return Method::ProbabilisticOfTrivials;
    } else {
        return Method::VertexInterpolation;
    }
}

std::string methodToString(const Method& method) {
    switch (method) {
        case TrivialNormalFaceCentroid:
            return "Trivial Normal Face Centroid";
        case DualNormalFaceCentroid:
            return "Dual Normal Face Centroid";
        case CorrectedNormalFaceCentroid:
            return "Corrected Normal Face Centroid";
        case ProbabilisticOfTrivials:
            return "Probabilistic Of Trivials";
        case VertexInterpolation:
            return "Vertex Interpolation";
        default:
            return "Unknown";
    }
}



int main(int argc, char** argv)
{
    polyscope::init();

    auto params = SH3::defaultParameters() | SHG3::defaultParameters();
    std::string filename = argc > 1 ? argv[1] : "../DGtalObjects/bunny66.vol";
    double radius = argc > 2 ? std::atof( argv[2] ) : 10.0;

    auto distribType = argc > 3 ? argToDistribType(argv[3]) : DistributionType::HalfSphere;

    auto binImage = SH3::makeBinaryImage(filename, params);
    auto K = SH3::getKSpace(binImage);
    auto surface = SH3::makeDigitalSurface(binImage, K, params);
    auto primalSurface = *SH3::makePrimalSurfaceMesh(surface);

    auto polyBunny = registerSurface(primalSurface, "bunny");

    for (auto m: {Method::TrivialNormalFaceCentroid, Method::DualNormalFaceCentroid, Method::CorrectedNormalFaceCentroid}) {
        auto varifolds = computeVarifolds(binImage, surface, radius, distribType, m);

        auto nbElements = m == Method::DualNormalFaceCentroid ? primalSurface.nbVertices() : primalSurface.nbFaces();

        std::vector<RealVector> lcs;
        for (auto i = 0; i < nbElements; i++) {
            lcs.push_back(varifolds[i].curvature);
        }
        if (m == Method::DualNormalFaceCentroid) {
            polyBunny->addVertexVectorQuantity(methodToString(m) + " Local Curvatures", lcs);
        } else {
            polyBunny->addFaceVectorQuantity(methodToString(m) + " Local Curvatures", lcs);
        }

        std::vector<double> lcsNorm;
        for (auto i = 0; i < nbElements; i++) {
            lcsNorm.push_back(varifolds[i].planeNormal.dot(varifolds[i].curvature) > 0 ? varifolds[i].curvature.norm() : -varifolds[i].curvature.norm());
        }
        if (m == Method::DualNormalFaceCentroid) {
            for (auto i = 0; i < nbElements; i++) {
                auto position = primalSurface.position(i);
                auto sum = 0.;
                for (auto f = 0; f < nbElements; f++) {
                    if (f != i && primalSurface.vertexInclusionRatio(position, 1, f) > 0) {
                        sum += lcsNorm[f];
                    }
                }
                lcsNorm[i] = abs(lcsNorm[i]) * (sum < 0 ? -1 : 1);
            }
        } else {
            for (auto i = 0; i < nbElements; i++) {
                auto sum = 0.;
                for (auto f: primalSurface.computeFacesInclusionsInBall(1, i)) {
                    if (f.second > 0) {
                        sum += lcsNorm[f.first];
                    }
                }
                lcsNorm[i] = abs(lcsNorm[i]) * (sum < 0 ? -1 : 1);
            }
        }

        auto minmax = std::minmax_element(lcsNorm.begin(), lcsNorm.end());
        DGtal::trace.info() << "Min: " << *minmax.first << " Max: " << *minmax.second << std::endl;
        const auto colormap = makeColorMap(*minmax.first, *minmax.second);
        std::vector<std::vector<double>> colorLcsNorm;
        for (auto i = 0; i < nbElements; i++) {
            const auto color = lcsNorm[i] < 0 ? colormap.first(lcsNorm[i]) : colormap.second(lcsNorm[i]);
            colorLcsNorm.push_back({static_cast<double>(color.red())/255, static_cast<double>(color.green())/255, static_cast<double>(color.blue())/255});
        }
        if (m == Method::DualNormalFaceCentroid) {
            polyBunny->addVertexColorQuantity(methodToString(m) + " Local Curvatures Norm", colorLcsNorm);
        } else {
            polyBunny->addFaceColorQuantity(methodToString(m) + " Local Curvatures Norm", colorLcsNorm);
        }
    }

    polyscope::show();
    return 0;
}
