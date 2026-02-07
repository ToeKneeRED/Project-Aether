using C2M;
using PhilLibX.IO;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using Vector2 = System.Numerics.Vector2;

namespace BlackOps2;

public class ClipMap
{
    public List<ClipMapMaterial> materials;
    public List<ClipMapBrush> brushes;
}

public sealed class ClipMapBrushSide
{
    public System.Numerics.Vector3 normal;
    public System.Numerics.Vector3[] verts = System.Array.Empty<System.Numerics.Vector3>();
    public string material;
    public int sideIndex;
}

public sealed class ClipMapBrush
{
    public uint brushIndex;
    public List<ClipMapBrushSide> sides = new();
}

public sealed class ClipMapTriangle
{
    public string material;
    public System.Numerics.Vector3 v0;
    public System.Numerics.Vector3 v1;
    public System.Numerics.Vector3 v2;
}

public sealed class ClipMapMaterial
{
    public string name;
    public int surfaceFlags;
    public int contentFlags;
}

public sealed class ClipMapPatch
{
    public string Material;
    public int Columns;
    public int Rows;
    public System.Numerics.Vector3[,] ControlPoints;
}

class ClipMapReader
{
    public struct cbrush_t
    {
        public System.Numerics.Vector3 mins;
        // public int contents;
        public System.Numerics.Vector3 maxs;
        public uint numsides;
        public List<cbrushside_t> sides;
        public List<int> axial_cflags; // [2][3]
        public List<int> axial_sflags; // [2][3]
        // public uint numverts;
        // public List<System.Numerics.Vector3> verts;
    }

    public struct cbrushside_t
    {
        public cplane_s plane;
        // public int cflags;
        public int sflags;
    }

    public struct cplane_s
    {
        public System.Numerics.Vector3 normal;
        public float dist;
        public byte type;
        public byte signbits;
    }

    // Helper struct for storing brush points with their side indices
    private struct ShowCollisionBrushPt
    {
        public System.Numerics.Vector3 xyz;
        public short sideIndex0;
        public short sideIndex1;
        public short sideIndex2;
    }

    // Axial plane representation
    private struct AxialPlane
    {
        public System.Numerics.Vector3 normal;
        public float dist;
    }

    // Winding structure for face construction
    private struct Winding
    {
        public int numpoints;
        public System.Numerics.Vector3[] p;

        public Winding(int capacity)
        {
            numpoints = 0;
            p = new System.Numerics.Vector3[capacity];
        }
    }

    private const int MAX_BRUSH_POINTS = 1024;
    private const float SNAP_GRID = 0.25f;
    private const float SNAP_EPSILON = 0.0099999998f;
    private const float PLANE_EPSILON = 0.1f;
    private const float POINT_EPSILON = 0.1f;

    /// <summary>
    /// Gets plane in vec4 form from either axial planes or brush sides
    /// </summary>
    private static void CM_GetPlaneVec4Form(
        cbrushside_t[] sides,
        AxialPlane[] axialPlanes,
        int index,
        Span<float> expandedPlane)
    {
        if (index >= 6)
        {
            // Non-axial plane from brush side
            var plane = sides[index - 6].plane;
            expandedPlane[0] = plane.normal.X;
            expandedPlane[1] = plane.normal.Y;
            expandedPlane[2] = plane.normal.Z;
            expandedPlane[3] = plane.dist;
        }
        else
        {
            // Axial plane
            var plane = axialPlanes[index];
            expandedPlane[0] = plane.normal.X;
            expandedPlane[1] = plane.normal.Y;
            expandedPlane[2] = plane.normal.Z;
            expandedPlane[3] = plane.dist;
        }
    }

    /// <summary>
    /// Intersects three planes to find a point
    /// </summary>
    private static bool IntersectPlanes(
        ReadOnlySpan<float> plane0,
        ReadOnlySpan<float> plane1,
        ReadOnlySpan<float> plane2,
        Span<float> xyz)
    {
        // Calculate determinant using cross products
        float determinant = 
            ((plane1[1] * plane2[2] - plane2[1] * plane1[2]) * plane0[0]) +
            ((plane2[1] * plane0[2] - plane0[1] * plane2[2]) * plane1[0]) +
            ((plane0[1] * plane1[2] - plane1[1] * plane0[2]) * plane2[0]);

        if (MathF.Abs(determinant) < 0.001f)
            return false;

        float invDet = 1.0f / determinant;

        // Calculate intersection point
        xyz[0] = (
            ((plane1[1] * plane2[2] - plane2[1] * plane1[2]) * plane0[3]) +
            ((plane2[1] * plane0[2] - plane0[1] * plane2[2]) * plane1[3]) +
            ((plane0[1] * plane1[2] - plane1[1] * plane0[2]) * plane2[3])
        ) * invDet;

        xyz[1] = (
            ((plane1[2] * plane2[0] - plane2[2] * plane1[0]) * plane0[3]) +
            ((plane2[2] * plane0[0] - plane0[2] * plane2[0]) * plane1[3]) +
            ((plane0[2] * plane1[0] - plane1[2] * plane0[0]) * plane2[3])
        ) * invDet;

        xyz[2] = (
            ((plane1[0] * plane2[1] - plane2[0] * plane1[1]) * plane0[3]) +
            ((plane2[0] * plane0[1] - plane0[0] * plane2[1]) * plane1[3]) +
            ((plane0[0] * plane1[1] - plane1[0] * plane0[1]) * plane2[3])
        ) * invDet;

        return true;
    }

    /// <summary>
    /// Snaps point to grid if it's close enough to planes
    /// </summary>
    private static void SnapPointToIntersectingPlanes(
        ReadOnlySpan<float> plane0,
        ReadOnlySpan<float> plane1,
        ReadOnlySpan<float> plane2,
        Span<float> xyz,
        float snapGrid,
        float snapEpsilon)
    {
        Span<float> snapped = stackalloc float[3];
        float invGrid = 1.0f / snapGrid;

        // Snap each axis to grid
        for (int axis = 0; axis < 3; axis++)
        {
            float rounded = MathF.Round(xyz[axis] * invGrid) * snapGrid;
            float delta = MathF.Abs(rounded - xyz[axis]);
            snapped[axis] = (delta <= snapEpsilon) ? rounded : xyz[axis];
        }

        // If no change, return early
        if (snapped[0] == xyz[0] && snapped[1] == xyz[1] && snapped[2] == xyz[2])
            return;

        // Calculate plane distances for both points
        float maxSnapErr = 0.0f;
        float maxBaseErr = snapEpsilon;

        for (int k = 0; k < 3; k++)
        {
            ReadOnlySpan<float> plane = k == 0 ? plane0 : (k == 1 ? plane1 : plane2);

            float snapErr = MathF.Abs(
                plane[0] * snapped[0] + plane[1] * snapped[1] + plane[2] * snapped[2] - plane[3]
            );
            if (snapErr > maxSnapErr)
                maxSnapErr = snapErr;

            float baseErr = MathF.Abs(
                plane[0] * xyz[0] + plane[1] * xyz[1] + plane[2] * xyz[2] - plane[3]
            );
            if (baseErr > maxBaseErr)
                maxBaseErr = baseErr;
        }

        // Use snapped if it's not worse
        if (maxBaseErr > maxSnapErr)
        {
            xyz[0] = snapped[0];
            xyz[1] = snapped[1];
            xyz[2] = snapped[2];
        }
    }

    /// <summary>
    /// Adds a point if it's inside all brush planes
    /// </summary>
    private static int CM_AddSimpleBrushPoint(
        cbrush_t brush,
        AxialPlane[] axialPlanes,
        ReadOnlySpan<short> sideIndices,
        ReadOnlySpan<float> xyz,
        int ptCount,
        Span<ShowCollisionBrushPt> brushPts)
    {
        // Check against axial planes first
        for (int i = 0; i < 6; i++)
        {
            float dist = 
                axialPlanes[i].normal.X * xyz[0] +
                axialPlanes[i].normal.Y * xyz[1] +
                axialPlanes[i].normal.Z * xyz[2] -
                axialPlanes[i].dist;

            if (dist > PLANE_EPSILON)
                return ptCount; // Outside this plane
        }

        // Check against brush sides
        for (int i = 0; i < brush.numsides; i++)
        {
            var plane = brush.sides[i].plane;

            // Skip if this is one of the three planes we're intersecting
            bool isIntersecting = false;
            for (int j = 0; j < 3; j++)
            {
                if (sideIndices[j] >= 6 && sideIndices[j] - 6 == i)
                {
                    isIntersecting = true;
                    break;
                }
            }

            if (isIntersecting)
                continue;

            float dist =
                plane.normal.X * xyz[0] +
                plane.normal.Y * xyz[1] +
                plane.normal.Z * xyz[2] -
                plane.dist;

            if (dist > PLANE_EPSILON)
                return ptCount; // Outside this plane
        }

        // Point is valid, add it
        if (ptCount >= MAX_BRUSH_POINTS)
            return ptCount;

        brushPts[ptCount] = new ShowCollisionBrushPt
        {
            xyz = new System.Numerics.Vector3(xyz[0], xyz[1], xyz[2]),
            sideIndex0 = sideIndices[0],
            sideIndex1 = sideIndices[1],
            sideIndex2 = sideIndices[2]
        };

        return ptCount + 1;
    }

    /// <summary>
    /// Main function: Intersects all plane combinations to generate brush vertices
    /// </summary>
    private static int CM_ForEachBrushPlaneIntersection(
        cbrush_t brush,
        AxialPlane[] axialPlanes,
        Span<ShowCollisionBrushPt> brushPts)
    {
        int ptCount = 0;
        int sideCount = (int)brush.numsides + 6;

        Span<short> sideIndices = stackalloc short[3];
        Span<float> plane0 = stackalloc float[4];
        Span<float> plane1 = stackalloc float[4];
        Span<float> plane2 = stackalloc float[4];
        Span<float> xyz = stackalloc float[3];

        // Triple nested loop: try all combinations of 3 planes
        for (sideIndices[0] = 0; sideIndices[0] < sideCount - 2; sideIndices[0]++)
        {
            CM_GetPlaneVec4Form(brush.sides.ToArray(), axialPlanes, sideIndices[0], plane0);

            for (sideIndices[1] = (short)(sideIndices[0] + 1); sideIndices[1] < sideCount - 1; sideIndices[1]++)
            {
                // Skip if same plane (for non-axial planes)
                if (sideIndices[0] >= 6 && sideIndices[1] >= 6)
                {
                    if (ReferenceEquals(
                        brush.sides[sideIndices[0] - 6].plane,
                        brush.sides[sideIndices[1] - 6].plane))
                        continue;
                }

                CM_GetPlaneVec4Form(brush.sides.ToArray(), axialPlanes, sideIndices[1], plane1);

                for (sideIndices[2] = (short)(sideIndices[1] + 1); sideIndices[2] < sideCount; sideIndices[2]++)
                {
                    // Skip if same plane
                    if ((sideIndices[0] >= 6 && sideIndices[2] >= 6 &&
                         ReferenceEquals(brush.sides[sideIndices[0] - 6].plane, brush.sides[sideIndices[2] - 6].plane)) ||
                        (sideIndices[1] >= 6 && sideIndices[2] >= 6 &&
                         ReferenceEquals(brush.sides[sideIndices[1] - 6].plane, brush.sides[sideIndices[2] - 6].plane)))
                        continue;

                    CM_GetPlaneVec4Form(brush.sides.ToArray(), axialPlanes, sideIndices[2], plane2);

                    // Try to intersect the three planes
                    if (IntersectPlanes(plane0, plane1, plane2, xyz))
                    {
                        // Snap to grid if close
                        SnapPointToIntersectingPlanes(plane0, plane1, plane2, xyz, SNAP_GRID, SNAP_EPSILON);

                        // Add point if it's inside all planes
                        ptCount = CM_AddSimpleBrushPoint(brush, axialPlanes, sideIndices, xyz, ptCount, brushPts);

                        if (ptCount >= MAX_BRUSH_POINTS - 1)
                            return 0; // Too many points, something is wrong
                    }
                }
            }
        }

        return ptCount;
    }

    /// <summary>
    /// Compares two vectors with custom epsilon
    /// </summary>
    private static bool VecNCompareCustomEpsilon(
        System.Numerics.Vector3 v0,
        System.Numerics.Vector3 v1,
        float epsilon)
    {
        float epsilonSq = epsilon * epsilon;
        
        for (int i = 0; i < 3; i++)
        {
            float diff = v0[i] - v1[i];
            if (diff * diff > epsilonSq)
                return false;
        }
        
        return true;
    }

    /// <summary>
    /// Gets list of unique XYZ points that touch a specific side
    /// </summary>
    private static int CM_GetXyzList(
        ReadOnlySpan<ShowCollisionBrushPt> pts,
        int ptCount,
        int sideIndex,
        Span<System.Numerics.Vector3> xyz,
        int xyzLimit)
    {
        int xyzCount = 0;

        for (int i = 0; i < ptCount; i++)
        {
            ref readonly var pt = ref pts[i];

            // Check if this point touches the side we're building
            if (sideIndex != pt.sideIndex0 && 
                sideIndex != pt.sideIndex1 && 
                sideIndex != pt.sideIndex2)
                continue;

            // Check for duplicates
            bool exists = false;
            for (int j = 0; j < xyzCount; j++)
            {
                if (VecNCompareCustomEpsilon(xyz[j], pt.xyz, POINT_EPSILON))
                {
                    exists = true;
                    break;
                }
            }

            if (exists)
                continue;

            if (xyzCount == xyzLimit)
                return xyzCount; // Hit limit

            xyz[xyzCount++] = pt.xyz;
        }

        return xyzCount;
    }

    /// <summary>
    /// Picks two axes to project the winding onto for 2D operations
    /// </summary>
    private static void PickProjectionAxes(ReadOnlySpan<float> normal, out int i, out int j)
    {
        // Find the dominant axis
        int k = 0;
        if (MathF.Abs(normal[1]) > MathF.Abs(normal[0])) k = 1;
        if (MathF.Abs(normal[2]) > MathF.Abs(normal[k])) k = 2;

        // Pick the other two axes
        i = (~k) & 1;
        j = (~k) & 2;
    }

    /// <summary>
    /// Calculates signed area of triangle projected onto 2D plane
    /// </summary>
    private static float SignedAreaProjected(
        in System.Numerics.Vector3 pt0,
        in System.Numerics.Vector3 pt1,
        in System.Numerics.Vector3 pt2,
        int i, int j)
    {
        return (pt2[j] - pt1[j]) * pt0[i] + 
               (pt0[j] - pt2[j]) * pt1[i] + 
               (pt1[j] - pt0[j]) * pt2[i];
    }

    /// <summary>
    /// Handles colinear point insertion into winding
    /// </summary>
    private static void CM_AddColinearExteriorPointToWindingProjected(
        ref Winding w,
        in System.Numerics.Vector3 pt,
        int i, int j,
        int index0, int index1)
    {
        // Determine which axis has larger delta
        float deltaI = w.p[index1][i] - w.p[index0][i];
        float deltaJ = w.p[index1][j] - w.p[index0][j];

        int axis;
        float delta;

        if (MathF.Abs(deltaI) < MathF.Abs(deltaJ))
        {
            axis = j;
            delta = deltaJ;
        }
        else
        {
            axis = i;
            delta = deltaI;
        }

        // Replace endpoint if new point extends the edge
        if (delta <= 0.0f)
        {
            if (pt[axis] <= w.p[index0][axis])
            {
                if (w.p[index1][axis] > pt[axis])
                    w.p[index1] = pt;
            }
            else
            {
                w.p[index0] = pt;
            }
        }
        else
        {
            if (w.p[index0][axis] <= pt[axis])
            {
                if (pt[axis] > w.p[index1][axis])
                    w.p[index1] = pt;
            }
            else
            {
                w.p[index0] = pt;
            }
        }
    }

    /// <summary>
    /// Adds an exterior point to winding, building convex hull
    /// </summary>
    private static void CM_AddExteriorPointToWindingProjected(
        ref Winding w,
        in System.Numerics.Vector3 pt,
        int i, int j)
    {
        int bestIdx = -1;
        float bestSigned = float.MaxValue;
        int indexPrev = w.numpoints - 1;

        // Find edge with most negative signed area (most "outside")
        for (int index = 0; index < w.numpoints; index++)
        {
            float signedArea = SignedAreaProjected(w.p[indexPrev], pt, w.p[index], i, j);

            if (signedArea < bestSigned)
            {
                bestSigned = signedArea;
                bestIdx = index;
            }

            indexPrev = index;
        }

        if (bestSigned < -0.001f)
        {
            // Point is outside - insert it
            Array.Copy(w.p, bestIdx, w.p, bestIdx + 1, w.numpoints - bestIdx);
            w.p[bestIdx] = pt;
            w.numpoints++;
        }
        else if (bestSigned <= 0.001f)
        {
            // Point is colinear - handle specially
            int prevIdx = (bestIdx + w.numpoints - 1) % w.numpoints;
            CM_AddColinearExteriorPointToWindingProjected(ref w, pt, i, j, prevIdx, bestIdx);
        }
        // else: point is inside, ignore it
    }

    /// <summary>
    /// Finds representative triangle from winding for validation
    /// </summary>
    private static float CM_RepresentativeTriangleFromWinding(
        in Winding w,
        ReadOnlySpan<float> normal,
        out int i0, out int i1, out int i2)
    {
        float areaBest = 0.0f;
        i0 = 0; i1 = 1; i2 = 2;

        // Try all triangle combinations
        for (int k = 2; k < w.numpoints; k++)
        {
            for (int j = 1; j < k; j++)
            {
                var vb = w.p[k] - w.p[j];

                for (int i = 0; i < j; i++)
                {
                    var va = w.p[i] - w.p[j];
                    var vc = System.Numerics.Vector3.Cross(vb, va);

                    float area = MathF.Abs(
                        vc.X * normal[0] + 
                        vc.Y * normal[1] + 
                        vc.Z * normal[2]
                    );

                    // Update best (note: any positive overwrites)
                    if (area > 0.0f)
                    {
                        areaBest = area;
                        i0 = i; i1 = j; i2 = k;
                    }
                }
            }
        }

        return areaBest;
    }

    /// <summary>
    /// Calculates plane from three points
    /// </summary>
    private static bool PlaneFromPoints(
        Span<float> plane,
        in System.Numerics.Vector3 v0,
        in System.Numerics.Vector3 v1,
        in System.Numerics.Vector3 v2)
    {
        var v1_v0 = v1 - v0;
        var v2_v0 = v2 - v0;
        var normal = System.Numerics.Vector3.Cross(v2_v0, v1_v0);

        float lengthSq = normal.LengthSquared();

        if (lengthSq < 2.0f)
        {
            if (lengthSq == 0.0f)
                return false;

            // Try alternate triangle if degenerate
            if (v2_v0.LengthSquared() * v1_v0.LengthSquared() * 0.0000010000001f >= lengthSq)
            {
                v1_v0 = v2 - v1;
                v2_v0 = v0 - v1;
                normal = System.Numerics.Vector3.Cross(v2_v0, v1_v0);

                if (v2_v0.LengthSquared() * v1_v0.LengthSquared() * 0.0000010000001f >= lengthSq)
                    return false;
            }
        }

        float length = MathF.Sqrt(lengthSq);
        normal /= length;

        plane[0] = normal.X;
        plane[1] = normal.Y;
        plane[2] = normal.Z;
        plane[3] = System.Numerics.Vector3.Dot(normal, v0);

        return true;
    }

    /// <summary>
    /// Reverses winding order
    /// </summary>
    private static void CM_ReverseWinding(ref Winding w)
    {
        int half = w.numpoints / 2;
        for (int i = 0; i < half; i++)
        {
            (w.p[i], w.p[w.numpoints - 1 - i]) = (w.p[w.numpoints - 1 - i], w.p[i]);
        }
    }

    /// <summary>
    /// Builds an ordered winding for a specific brush side
    /// </summary>
    private static bool CM_BuildBrushWindingForSide(
        ref Winding winding,
        ReadOnlySpan<ShowCollisionBrushPt> pts,
        ReadOnlySpan<float> planeNormal,
        int sideIndex,
        int ptCount)
    {
        // Get all points that touch this side
        Span<System.Numerics.Vector3> xyz = stackalloc System.Numerics.Vector3[1024];
        int xyzCount = CM_GetXyzList(pts, ptCount, sideIndex, xyz, 1024);

        if (xyzCount < 3)
            return false; // Not enough points

        // Pick projection axes
        PickProjectionAxes(planeNormal, out int i, out int j);

        // Start with first two points
        winding.p[0] = xyz[0];
        winding.p[1] = xyz[1];
        winding.numpoints = 2;

        // Add remaining points, building convex hull
        for (int k = 2; k < xyzCount; k++)
        {
            CM_AddExteriorPointToWindingProjected(ref winding, xyz[k], i, j);
        }

        // Validate winding by finding representative triangle
        if (CM_RepresentativeTriangleFromWinding(winding, planeNormal, out int i0, out int i1, out int i2) < 0.001f)
            return false; // Degenerate

        // Calculate plane from triangle and check orientation
        Span<float> tmpPlane = stackalloc float[4];
        if (!PlaneFromPoints(tmpPlane, winding.p[i0], winding.p[i1], winding.p[i2]))
            return false;

        // Reverse if facing wrong way
        float dot = tmpPlane[0] * planeNormal[0] + 
                    tmpPlane[1] * planeNormal[1] + 
                    tmpPlane[2] * planeNormal[2];

        if (dot < 0.0f)
            CM_ReverseWinding(ref winding);

        return true;
    }

    // Static cache for caulk surface flags
    private static int s_caulk_sflags = 0x7FFFFFFF;

    /// <summary>
    /// Gets the material name from surface flags by looking up in material list
    /// </summary>
    public static string GetMaterialNameFromSFlags(int sflags, List<ClipMapMaterial> materials)
    {
        foreach (var mat in materials)
        {
            if (mat.surfaceFlags == sflags)
                return mat.name;
        }
        return "caulk"; // Fallback
    }

    /// <summary>
    /// Gets material for a brush following the game's logic
    /// Returns the surface flags value to look up
    /// </summary>
    public static int GetMaterialFromBrush(cbrush_t brush, ClipMapMaterial[] materials)
    {
        // Find caulk material surface flags (cache it)
        if (s_caulk_sflags == 0x7FFFFFFF)
        {
            s_caulk_sflags = 2147483646;
            foreach (var mat in materials)
            {
                if (mat.name == "caulk")
                {
                    s_caulk_sflags = mat.surfaceFlags;
                    break;
                }
            }
        }

        // Get top face material (axial_sflags[1][2])
        int sflags = brush.axial_sflags[1 * 3 + 2];

        // If top face is caulk, find best non-caulk material
        if (sflags == s_caulk_sflags)
        {
            sflags = 0x7FFFFFFF;
            float bestZ = -10.0f;
            for ( var j = 0; j < brush.numsides; ++j )
            {
                if ( brush.sides[j].sflags != s_caulk_sflags )
                {
                    var plane = brush.sides[j].plane;
                    if ( (float)((float)((float)(plane.normal[0] * 0.0) + (float)(plane.normal[1] * 0.0))
                                 + (float)(plane.normal[2] * 1.0)) > bestZ )
                    {
                        bestZ = (float)((float)(plane.normal[0] * 0.0) + (float)(plane.normal[1] * 0.0))
                                + (float)(plane.normal[2] * 1.0);
                        sflags = brush.sides[j].sflags;
                    }
                }
            }
            if ( sflags == 0x7FFFFFFF )
            {
                for ( var k = 0; k < 2; ++k )
                {
                    for (var m = 0; m < 3; ++m )
                    {
                        if ( brush.axial_sflags[k * 3 + m] != s_caulk_sflags )
                        {
                            sflags = brush.axial_sflags[k * 3 + m];
                            return sflags;
                        }
                    }
                }
                sflags = s_caulk_sflags;
            }
        }

        return sflags;
    }

    /// <summary>
    /// Gets material for a specific brush side
    /// </summary>
    private static string GetMaterialForBrushSide(
        cbrush_t brush,
        int sideIndex,
        List<ClipMapMaterial> materials)
    {
        int sflags;

        // Axial planes (indices 0-5)
        if (sideIndex < 6)
        {
            // Map side index to axial_sflags array position
            // Bottom=0, Top=1, Left=2, Right=3, Front=4, Back=5
            // Corresponds to axial_sflags[array][index] where array*3+index
            switch (sideIndex)
            {
                case 0: sflags = brush.axial_sflags[0 * 3 + 2]; break; // Bottom (-Z)
                case 1: sflags = brush.axial_sflags[1 * 3 + 2]; break; // Top (+Z)
                case 2: sflags = brush.axial_sflags[0 * 3 + 1]; break; // Left (-Y) 
                case 3: sflags = brush.axial_sflags[1 * 3 + 0]; break; // Right (+X)
                case 4: sflags = brush.axial_sflags[1 * 3 + 1]; break; // Front (+Y)
                case 5: sflags = brush.axial_sflags[0 * 3 + 0]; break; // Back (-X)
                default: sflags = 0x7FFFFFFF; break;
            }
        }
        // Brush sides (indices 6+)
        else
        {
            int brushSideIndex = sideIndex - 6;
            if (brushSideIndex < brush.numsides)
                sflags = brush.sides[brushSideIndex].sflags;
            else
                sflags = 0x7FFFFFFF;
        }

        return GetMaterialNameFromSFlags(sflags, materials);
    }

    /// <summary>
    /// Entry point: Converts a cbrush_t to a ClipMapBrush with valid vertices and materials
    /// </summary>
    public static ClipMapBrush CM_ShowSingleBrushCollision(
        cbrush_t brush, 
        uint brushIndex,
        List<ClipMapMaterial> materials)
    {
        // Reset caulk cache for new brush processing
        s_caulk_sflags = 0x7FFFFFFF;

        // Create axial planes from brush bounds
        AxialPlane[] axialPlanes = new AxialPlane[6];
        axialPlanes[0] = new AxialPlane { normal = new(-1, 0, 0), dist = -brush.mins.X };
        axialPlanes[1] = new AxialPlane { normal = new( 1, 0, 0), dist =  brush.maxs.X };
        axialPlanes[2] = new AxialPlane { normal = new( 0,-1, 0), dist = -brush.mins.Y };
        axialPlanes[3] = new AxialPlane { normal = new( 0, 1, 0), dist =  brush.maxs.Y };
        axialPlanes[4] = new AxialPlane { normal = new( 0, 0,-1), dist = -brush.mins.Z };
        axialPlanes[5] = new AxialPlane { normal = new( 0, 0, 1), dist =  brush.maxs.Z };

        // Generate all brush points from plane intersections
        Span<ShowCollisionBrushPt> brushPts = stackalloc ShowCollisionBrushPt[MAX_BRUSH_POINTS];
        int ptCount = CM_ForEachBrushPlaneIntersection(brush, axialPlanes, brushPts);

        if (ptCount < 4)
            return null; // Not enough points for a valid brush

        // Create result brush
        var clipBrush = new ClipMapBrush { brushIndex = brushIndex };

        // Create winding for building faces
        var winding = new Winding(256);

        // Process axial planes (indices 0-5)
        for (int s = 0; s < 6; s++)
        {
            Span<float> normal = stackalloc float[3] 
            { 
                axialPlanes[s].normal.X, 
                axialPlanes[s].normal.Y, 
                axialPlanes[s].normal.Z 
            };

            if (CM_BuildBrushWindingForSide(ref winding, brushPts[..ptCount], normal, s, ptCount))
            {
                var side = new ClipMapBrushSide
                {
                    normal = new System.Numerics.Vector3(normal[0], normal[1], normal[2]),
                    sideIndex = s,
                    material = GetMaterialForBrushSide(brush, s, materials),
                    verts = new System.Numerics.Vector3[winding.numpoints]
                };

                for (int v = 0; v < winding.numpoints; v++)
                {
                    side.verts[v] = winding.p[v];
                }

                clipBrush.sides.Add(side);
            }
        }

        // Process additional brush sides (indices 6+)
        for (int s = 0; s < brush.numsides; s++)
        {
            var plane = brush.sides[s].plane;
            Span<float> normal = stackalloc float[3] 
            { 
                plane.normal.X, 
                plane.normal.Y, 
                plane.normal.Z 
            };

            int sideIndex = s + 6;

            if (CM_BuildBrushWindingForSide(ref winding, brushPts[..ptCount], normal, sideIndex, ptCount))
            {
                var side = new ClipMapBrushSide
                {
                    normal = new System.Numerics.Vector3(normal[0], normal[1], normal[2]),
                    sideIndex = sideIndex,
                    material = GetMaterialForBrushSide(brush, sideIndex, materials),
                    verts = new System.Numerics.Vector3[winding.numpoints]
                };

                for (int v = 0; v < winding.numpoints; v++)
                {
                    side.verts[v] = winding.p[v];
                }

                clipBrush.sides.Add(side);
            }
        }
        
        var brushMat =  GetMaterialNameFromSFlags(GetMaterialFromBrush(brush, materials.ToArray()), materials);
        foreach (var side in clipBrush.sides)
            side.material = brushMat;

        return clipBrush;
    }
}