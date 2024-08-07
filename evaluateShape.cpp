
#include <iostream>
#include <algorithm>
#include "DGtal/base/Common.h"
#include "DGtal/shapes/SurfaceMesh.h"
#include "DGtal/geometry/meshes/CorrectedNormalCurrentComputer.h"
#include "DGtal/helpers/Shortcuts.h"
#include "DGtal/helpers/ShortcutsGeometry.h"
#include "DGtal/io/writers/SurfaceMeshWriter.h"
#include "DGtal/io/colormaps/GradientColorMap.h"
#include "DGtal/io/colormaps/QuantifiedColorMap.h"
#include "core.cpp"

void usage(char *argv[])
{
    using namespace DGtal;
    using namespace DGtal::Z3i;
    typedef Shortcuts< KSpace >          SH;
    std::cout << "Usage: " << std::endl
              << "\t" << argv[ 0 ] << " <P> <B> <h> <R> <kernel> <method>" << std::endl
              << std::endl
              << "Computation of mean and Gaussian curvatures on an "      << std::endl
              << "digitized implicit shape using constant or "             << std::endl
              << "interpolated corrected curvature measures (based "       << std::endl
              << "on the theory of corrected normal currents)."            << std::endl
              << "- builds the surface mesh from polynomial <P>"           << std::endl
              << "- <B> defines the digitization space size [-B,B]^3"      << std::endl
              << "- <h> is the gridstep digitization"                      << std::endl
              << "- <R> is the radius of the measuring balls"              << std::endl
              << "- <kernel> is the kernel used to sample the surface ('l': linear, 'p': polynomial, 'e': exponential, 'c': CNC like)" << std::endl
              << "- <method> is the method used to compute the curvature ('tnfc': trivial normal face centroid, 'cnfc': corrected normal face centroid)" << std::endl
              << "- If a 7th argument is provided which is not \"TEST\", the corrected normal current is computed and compared to the expected mean and Gaussian curvatures." << std::endl
              << "- If the 7th argument is \"TEST\", the 8th argument must be provided and will run a test. Please refer to TEST help for more information." << std::endl
              << std::endl
              << "It produces several OBJ files to display mean and"       << std::endl
              << "Gaussian curvature estimation results: `example-cnc-H.obj`" << std::endl
              << "and `example-cnc-G.obj` as well as the associated MTL file." << std::endl;
    std::cout << "You may either write your own polynomial as 3*x^2*y-z^2*x*y+1" << std::endl
              <<"or use a predefined polynomial in the following list:" << std::endl;
    auto L = SH::getPolynomialList();
    for ( const auto& p : L )
        std::cout << p.first << " : " << p.second << std::endl;
}

int main( int argc, char* argv[] )
{
    polyscope::init();
    if ( argc <= 1 )
    {
        usage(argv);
        return 0;
    }
    using namespace DGtal;
    using namespace DGtal::Z3i;
    typedef SurfaceMesh< RealPoint, RealVector >                    SM;
    typedef CorrectedNormalCurrentComputer< RealPoint, RealVector > CNC;
    typedef Shortcuts< KSpace >          SH;
    typedef ShortcutsGeometry< KSpace > SHG;
    std::string  poly = argv[ 1 ]; // polynomial
    const double    B = argc > 2 ? atof( argv[ 2 ] ) : 1.0; // max ||_oo bbox
    const double    h = argc > 3 ? atof( argv[ 3 ] ) : 1.0; // gridstep
    const double    R = argc > 4 ? atof( argv[ 4 ] ) : 2.0; // radius of measuring ball
    const auto kernel = argc > 5 ? argToDistribType( argv[ 5 ] ) : DistributionType::Exponential;
    const auto method = argc > 6 ? argToMethod( argv[ 6 ] ) : Method::CorrectedNormalFaceCentroid;
    const auto checkCNC = argc > 7;

    // Read polynomial and build digital surface
    auto params = SH::defaultParameters() | SHG::defaultParameters();
    params( "t-ring", 6 )( "surfaceTraversal", "Default" );
    params( "polynomial", poly )( "gridstep", h );
    params( "minAABB", -B )( "maxAABB", B );
    params( "offset", 3.0 );
    auto shape       = SH::makeImplicitShape3D( params );
    auto K           = SH::getKSpace( params );
    auto dshape      = SH::makeDigitizedImplicitShape3D( shape, params );
    auto bimage      = SH::makeBinaryImage( dshape, params );
    if ( bimage == nullptr )
    {
        trace.error() <<  "Unable to read polynomial <"
                      << poly.c_str() << ">" << std::endl;
        return 1;
    }
    auto sembedder   = SH::getSCellEmbedder( K );
    auto embedder    = SH::getCellEmbedder( K );
    auto surface     = SH::makeDigitalSurface( bimage, K, params );
    auto surfels     = SH::getSurfelRange( surface, params );
    trace.info() << "- surface has " << surfels.size()<< " surfels." << std::endl;

    SM smesh;
    std::vector< SM::Vertices > faces;
    SH::Cell2Index c2i;
    auto pointels = SH::getPointelRange( c2i, surface );
    auto vertices = SH::RealPoints( pointels.size() );
    std::transform( pointels.cbegin(), pointels.cend(), vertices.begin(),
                    [&] (const SH::Cell& c) { return h * embedder( c ); } );
    for ( auto&& surfel : *surface )
    {
        const auto primal_surfel_vtcs = SH::getPointelRange( K, surfel );
        SM::Vertices face;
        for ( auto&& primal_vtx : primal_surfel_vtcs )
            face.push_back( c2i[ primal_vtx ] );
        faces.push_back( face );
    }
    smesh.init( vertices.cbegin(), vertices.cend(),
                faces.cbegin(),    faces.cend() );
    trace.info() << smesh << std::endl;

    auto polysurf = registerSurface(smesh, "studied mesh");
    if (argc > 7 && std::string(argv[7]) == "TEST") {
        std::string testToDo = argc > 8 ? argv[8] : "help";
        if (testToDo == "kernel") {
            auto positions = SH3::RealPoints();
            for (auto f = 0; f < smesh.nbFaces(); ++f) {
                positions.push_back(smesh.faceCentroid(f));
            }

            auto kdTree = LinearKDTree<RealPoint, 3>(positions);
            auto center = kdTree.position(0);
            auto rd = RadialDistance(center, R, kernel/*, atof(argv[9])*/);
            auto indices = kdTree.pointsInBall(center, R);
            auto weights = rd(positions, indices);
            auto values = std::vector<double>(positions.size(), 0.0);
            auto derivvalues = std::vector<double>(positions.size(), 0.0);
            for (auto i = 0; i < indices.size(); ++i) {
                values[indices[i]] = weights[i].first;
                derivvalues[indices[i]] = weights[i].second;
            }
            polysurf->addFaceScalarQuantity("Radial Distance", values);
            polysurf->addFaceScalarQuantity("Radial Distance Derivative", derivvalues);
            polyscope::show();
        } else {
            if (testToDo != "help") {
                std::cout << "Unknown test: " << testToDo << std::endl;
            }
            std::cout << "Available tests: " << std::endl
                    << "- kernel : plot the returned weights of the kernel function centered around the face 0 of the object"
                    << std::endl;
        }

        return 0;
    }

    SH3::RealVectors face_normals;
    if (checkCNC) {
        face_normals = SHG3::getIINormalVectors(bimage, surfels, params);
        polysurf->addFaceVectorQuantity("Used Normals", face_normals);
    }

    bool v3Enabled = false;
    std::vector<Varifold> varifolds = computeVarifoldsV2(bimage, surface, R, kernel, method, h, 5.0, params, face_normals);

//    bool v3Enabled = true;
//    std::vector<Varifold> varifolds = computeVarifoldsV3(bimage, surface, R, kernel, method, h, 5.0, params, face_normals);

    std::vector< double > H( varifolds.size() );
    std::vector< double > G( varifolds.size() );
    H = computeSignedNorms(smesh, varifolds, method);
    if (v3Enabled) {
        G = computeGaussianCurvaturesV3(varifolds);
    }

    auto exp_H = SHG::getMeanCurvatures( shape, K, surfels, params );
    auto exp_G = SHG::getGaussianCurvatures( shape, K, surfels, params );

    auto H_min_max = std::minmax_element( H.cbegin(), H.cend() );
    auto G_min_max = std::minmax_element( G.cbegin(), G.cend() );
    auto exp_H_min_max = std::minmax_element( exp_H.cbegin(), exp_H.cend() );
    auto exp_G_min_max = std::minmax_element( exp_G.cbegin(), exp_G.cend() );
    std::cout << "Expected mean curvatures:"
              << " min=" << *exp_H_min_max.first << " max=" << *exp_H_min_max.second
              << std::endl;
    std::cout << "Computed mean curvatures:"
              << " min=" << *H_min_max.first << " max=" << *H_min_max.second
              << std::endl;
    std::cout << "Expected Gaussian curvatures:"
              << " min=" << *exp_G_min_max.first << " max=" << *exp_G_min_max.second
              << std::endl;
    std::cout << "Computed Gaussian curvatures:"
              << " min=" << *G_min_max.first << " max=" << *G_min_max.second
              << std::endl;

    const auto      error_H = SHG::getScalarsAbsoluteDifference( H, exp_H );
    const auto stat_error_H = SHG::getStatistic( error_H );
    const auto   error_H_l2 = SHG::getScalarsNormL2( H, exp_H );
    trace.info() << "|He-H|_oo = " << stat_error_H.max() << std::endl;
    trace.info() << "|He-H|_2  = " << error_H_l2 << std::endl;
    const auto      error_G = SHG::getScalarsAbsoluteDifference( G, exp_G );
    const auto stat_error_G = SHG::getStatistic( error_G );
    const auto   error_G_l2 = SHG::getScalarsNormL2( G, exp_G );
    trace.info() << "|Ge-G|_oo = " << stat_error_G.max() << std::endl;
    trace.info() << "|Ge-G|_2  = " << error_G_l2 << std::endl;

    // Remove normals for better blocky display.
    smesh.vertexNormals() = SH::RealVectors();
    smesh.faceNormals()   = SH::RealVectors();

    if (checkCNC) {
        // Builds a CorrectedNormalCurrentComputer object onto the SurfaceMesh object
        CNC cnc(smesh);
        smesh.setFaceNormals(face_normals.cbegin(), face_normals.cend()); // CCNC
        // computes area, mean and Gaussian curvature measures
        auto mu0 = cnc.computeMu0();
        auto mu1 = cnc.computeMu1();
        auto mu2 = cnc.computeMu2();
        // estimates mean (H) and Gaussian (G) curvatures by measure normalization.
        std::vector<double> H_CNC(varifolds.size());
        std::vector<double> G_CNC(varifolds.size());

        for (auto f = 0; f < varifolds.size(); ++f) {
            const auto b = smesh.faceCentroid(f);
            const auto area = mu0.measure(b, R, f);
            H_CNC[f] = cnc.meanCurvature(area, mu1.measure(b, R, f));
            G_CNC[ f ] = cnc.GaussianCurvature( area, mu2.measure( b, R, f ) );
        }
        auto H_CNC_min_max = std::minmax_element( H_CNC.cbegin(), H_CNC.cend() );
        auto G_CNC_min_max = std::minmax_element( G_CNC.cbegin(), G_CNC.cend() );
        std::cout << "CNC computed mean curvatures:"
                  << " min=" << *H_CNC_min_max.first << " max=" << *H_CNC_min_max.second
                  << std::endl;
        std::cout << "CNC computed Gaussian curvatures:"
                  << " min=" << *G_CNC_min_max.first << " max=" << *G_CNC_min_max.second
                  << std::endl;
        const auto      error_H_CNC = SHG::getScalarsAbsoluteDifference( H_CNC, exp_H );
        const auto stat_error_H_CNC = SHG::getStatistic( error_H_CNC );
        const auto   error_H_CNC_l2 = SHG::getScalarsNormL2( H_CNC, exp_H );
        trace.info() << "|He-H_CNC|_oo = " << stat_error_H_CNC.max() << std::endl;
        trace.info() << "|He-H_CNC|_2  = " << error_H_CNC_l2 << std::endl;
        const auto      error_G_CNC = SHG::getScalarsAbsoluteDifference( G_CNC, exp_G );
        const auto stat_error_G_CNC = SHG::getStatistic( error_G_CNC );
        const auto   error_G_CNC_l2 = SHG::getScalarsNormL2( G_CNC, exp_G );
        trace.info() << "|Ge-G_CNC|_oo = " << stat_error_G_CNC.max() << std::endl;
        trace.info() << "|Ge-G_CNC|_2  = " << error_G_CNC_l2 << std::endl;
        polysurf->addFaceScalarQuantity("CNC H", H_CNC );
        polysurf->addFaceScalarQuantity("Error H He-H_CNC", error_H_CNC );
        if (v3Enabled) {
            polysurf->addFaceScalarQuantity("CNC G", G_CNC );
            polysurf->addFaceScalarQuantity("Error G Ge-G_CNC", error_G_CNC );
        }
    }

    SH3::RealVectors curvatures( varifolds.size() );
    SH3::RealVectors usedNormals( varifolds.size() );
    for ( auto i = 0; i < varifolds.size(); i++ )
    {
        curvatures[ i ] = varifolds[ i ].curvature;
        usedNormals[ i ] = varifolds[ i ].planeNormal;
    }

    if (v3Enabled) {
        polysurf->addFaceScalarQuantity("Computed G", G);
        polysurf->addFaceScalarQuantity("True G", exp_G);
        polysurf->addFaceScalarQuantity("Error G Ge-G", error_G);
    }
    polysurf->addFaceVectorQuantity("Local Curvature", curvatures);
    polysurf->addFaceScalarQuantity("Computed H", H );
    polysurf->addFaceScalarQuantity("True H", exp_H );
    polysurf->addFaceScalarQuantity("Error H He-H", error_H );
    polyscope::show();

    return 0;
}
