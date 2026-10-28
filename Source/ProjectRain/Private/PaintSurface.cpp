#include "PaintSurface.h"

#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "ProceduralMeshComponent.h"
#include "TextureResource.h"

namespace
{
	/** Cosine-weighted directions over the hemisphere around +Z (Hammersley points): the AO rays. The same set for
	 *  every texel, so the bake is smooth (no per-texel noise). */
	TArray<FVector> HemisphereRays(int32 Count)
	{
		TArray<FVector> Rays;
		for (int32 i = 0; i < Count; ++i)
		{
			uint32 Bits = uint32(i);
			Bits = (Bits << 16u) | (Bits >> 16u);
			Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
			Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
			Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
			Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
			const double U = (i + 0.5) / Count;
			const double V = double(Bits) * 2.3283064365386963e-10;
			const double R = FMath::Sqrt(U);
			const double Phi = 2.0 * PI * V;
			Rays.Add(FVector(R * FMath::Cos(Phi), R * FMath::Sin(Phi), FMath::Sqrt(FMath::Max(0.0, 1.0 - U))));
		}
		return Rays;
	}

	uint8 ToByte(float V)
	{
		return uint8(FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255));
	}
}

bool FPaintSurface::Inside(const FVector& P, int32 Skip) const
{
	constexpr double Eps = 0.01;
	for (int32 k = 0; k < Boxes.Num(); ++k)
	{
		const FPaintBox& B = Boxes[k];
		if (k != Skip && P.X > B.Min.X + Eps && P.X < B.Max.X - Eps && P.Y > B.Min.Y + Eps && P.Y < B.Max.Y - Eps && P.Z > B.Min.Z + Eps && P.Z < B.Max.Z - Eps)
		{
			return true;
		}
	}
	return false;
}

float FPaintSurface::RayHit(const FVector& From, const FVector& Dir, float MaxT, const TArray<int32>& Candidates) const
{
	float Best = MaxT;
	const FVector Inv(FMath::Abs(Dir.X) > 1e-8 ? 1.0 / Dir.X : 1e8, FMath::Abs(Dir.Y) > 1e-8 ? 1.0 / Dir.Y : 1e8, FMath::Abs(Dir.Z) > 1e-8 ? 1.0 / Dir.Z : 1e8);
	for (const int32 k : Candidates)
	{
		const FPaintBox& B = Boxes[k];
		const FVector T1 = (B.Min - From) * Inv;
		const FVector T2 = (B.Max - From) * Inv;
		const double Enter = FMath::Max3(FMath::Min(T1.X, T2.X), FMath::Min(T1.Y, T2.Y), FMath::Min(T1.Z, T2.Z));
		const double Exit = FMath::Min3(FMath::Max(T1.X, T2.X), FMath::Max(T1.Y, T2.Y), FMath::Max(T1.Z, T2.Z));
		if (Exit >= FMath::Max(Enter, 0.0) && Enter < Best)
		{
			Best = float(FMath::Max(Enter, 0.0));
		}
	}
	return Best;
}

bool FPaintSurface::FHole::Contains(double U, double V) const
{
	if (!bValid || V < 0.0)
	{
		return false;
	}
	if (V <= Spring)
	{
		return FMath::Abs(U - Cu) < R;
	}
	const double Y = V - Spring;
	if (bPointed)
	{
		// Two circles of radius 2R through the opposite springing points.
		return (U - Cu - R) * (U - Cu - R) + Y * Y < 4.0 * R * R && (U - Cu + R) * (U - Cu + R) + Y * Y < 4.0 * R * R;
	}
	return (U - Cu) * (U - Cu) + Y * Y < R * R;
}

TArray<FVector2D> FPaintSurface::ArcPoints(double Cu, double R, double Spring, bool bPointed, int32 Segments)
{
	// From the left springing point over the top to the right one (U increasing).
	const int32 N = FMath::Max(2, Segments / 2 * 2);
	TArray<FVector2D> P;
	if (bPointed)
	{
		// Left arc about (Cu + R) from 180 to 120 degrees, right arc about (Cu - R) from 60 to 0 degrees.
		for (int32 k = 0; k <= N / 2; ++k)
		{
			const double A = PI - (PI / 3.0) * k / (N / 2);
			P.Add(FVector2D(Cu + R + 2.0 * R * FMath::Cos(A), Spring + 2.0 * R * FMath::Sin(A)));
		}
		for (int32 k = 1; k <= N / 2; ++k)
		{
			const double A = PI / 3.0 - (PI / 3.0) * k / (N / 2);
			P.Add(FVector2D(Cu - R + 2.0 * R * FMath::Cos(A), Spring + 2.0 * R * FMath::Sin(A)));
		}
	}
	else
	{
		for (int32 k = 0; k <= N; ++k)
		{
			const double A = PI - PI * k / N;
			P.Add(FVector2D(Cu + R * FMath::Cos(A), Spring + R * FMath::Sin(A)));
		}
	}
	return P;
}

void FPaintSurface::AddFace(FFace F)
{
	if (F.W < 0.01f || F.H < 0.01f)
	{
		return;
	}
	const FPaintBox& B = Boxes[F.Box];
	F.NU = FMath::Max(1, FMath::CeilToInt(F.W / TexelSize));
	F.NV = FMath::Max(1, FMath::CeilToInt(F.H / TexelSize));
	F.First = Texels.Num();
	F.Visible = 0;
	for (int32 j = 0; j < F.NV; ++j)
	{
		for (int32 i = 0; i < F.NU; ++i)
		{
			FTexel T;
			const double Lu = (i + 0.5) * F.W / F.NU, Lv = (j + 0.5) * F.H / F.NV;
			const FVector P = F.Origin + F.U * Lu + F.V * Lv;
			T.P = FVector3f(P);
			T.Face = Faces.Num();
			T.bVisible = !F.Hole.Contains(Lu, Lv) && !Inside(P + F.N * 0.5, F.Skip);
			// Downward faces never show in the isometric view at any yaw: painted, but not part of the score.
			T.bTarget = B.Kind == EPaintBoxKind::Target && F.N.Z > -0.5;
			T.Area = float((F.W / F.NU) * (F.H / F.NV));
			F.Visible += T.bVisible ? 1 : 0;
			Texels.Add(T);
		}
	}
	if (F.Visible == 0)
	{
		Texels.SetNum(F.First);
		return;
	}
	Faces.Add(MoveTemp(F));
}

void FPaintSurface::AddArch(const FPaintArch& A, bool bFaces, int32& Owner)
{
	// Passage along axis Pa; the faces' own U runs across it (Ca), V up.
	const int32 Pa = A.Axis == 0 ? 0 : 1;
	const int32 Ca = 1 - Pa;
	FVector Along = FVector::ZeroVector, Across = FVector::ZeroVector;
	Along[Pa] = 1.0;
	Across[Ca] = 1.0;
	const double W = A.Max[Ca] - A.Min[Ca];
	const double H = A.Max.Z - A.Min.Z;
	const double D = A.Max[Pa] - A.Min[Pa];
	const double R = 0.5 * A.Width;
	const double Cu = A.Center;
	const TArray<FVector2D> Arc = ArcPoints(Cu, R, A.Spring, A.bPointed, A.Segments);
	const double Apex = A.Spring + (A.bPointed ? R * FMath::Sqrt(3.0) : R);
	auto At = [&](double U, double V, double S) { return A.Min + Across * U + FVector::UpVector * V + Along * S; };

	// The solid parts as proxies: the piers, the masonry above each soffit strip, and a lintel above the apex (the
	// coarse collision set is the piers and the lintel).
	auto Proxy = [&](const FVector& Mn, const FVector& Mx, bool bCollide)
	{
		FPaintBox& B = Boxes.AddDefaulted_GetRef();
		B.Min = Mn.ComponentMin(Mx);
		B.Max = Mn.ComponentMax(Mx);
		B.Kind = A.Kind;
		B.Palette = A.Palette;
		B.bCollide = bCollide;
		B.bProxy = true;
		return Boxes.Num() - 1;
	};
	if (!bFaces)
	{
		Owner = Proxy(At(0, 0, 0), At(Cu - R, H, D), true);
		Proxy(At(Cu + R, 0, 0), At(W, H, D), true);
		Proxy(At(Cu - R, Apex, 0), At(Cu + R, H, D), true);
		for (int32 k = 0; k + 1 < Arc.Num(); ++k)
		{
			Proxy(At(Arc[k].X, FMath::Max(Arc[k].Y, Arc[k + 1].Y), 0), At(Arc[k + 1].X, H, D), false);
		}
		return;
	}

	auto Face = [&](const FVector& Origin, const FVector& U, const FVector& V, double FW, double FH, const FVector& N)
	{
		FFace F;
		F.Box = Owner;
		F.Origin = Origin;
		F.U = U;
		F.V = V;
		F.N = N;
		F.W = float(FW);
		F.H = float(FH);
		return F;
	};
	// Front and back: the rectangle with the arch cut out (two piers and a band of quads over the arc).
	TArray<FVector2D> Poly;
	auto Quad = [&Poly](const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
	{
		Poly.Append({ P0, P1, P2, P0, P2, P3 });
	};
	Quad(FVector2D(0, 0), FVector2D(Cu - R, 0), FVector2D(Cu - R, H), FVector2D(0, H));
	Quad(FVector2D(Cu + R, 0), FVector2D(W, 0), FVector2D(W, H), FVector2D(Cu + R, H));
	for (int32 k = 0; k + 1 < Arc.Num(); ++k)
	{
		Quad(Arc[k], Arc[k + 1], FVector2D(Arc[k + 1].X, H), FVector2D(Arc[k].X, H));
	}
	for (const int32 Side : { 0, 1 })
	{
		FFace F = Face(At(0, 0, Side * D), Across, FVector::UpVector, W, H, Side ? Along : -Along);
		F.Hole.bValid = true;
		F.Hole.Cu = Cu;
		F.Hole.R = R;
		F.Hole.Spring = A.Spring;
		F.Hole.bPointed = A.bPointed;
		F.Poly = Poly;
		AddFace(F);
	}
	// Top, the two ends, the piers' feet.
	AddFace(Face(At(0, H, 0), Across, Along, W, D, FVector::UpVector));
	AddFace(Face(At(0, 0, 0), Along, FVector::UpVector, D, H, -Across));
	AddFace(Face(At(W, 0, 0), Along, FVector::UpVector, D, H, Across));
	AddFace(Face(At(0, 0, 0), Across, Along, Cu - R, D, -FVector::UpVector));
	AddFace(Face(At(Cu + R, 0, 0), Across, Along, W - Cu - R, D, -FVector::UpVector));
	// Jambs, facing into the passage.
	AddFace(Face(At(Cu - R, 0, 0), Along, FVector::UpVector, D, A.Spring, Across));
	AddFace(Face(At(Cu + R, 0, 0), Along, FVector::UpVector, D, A.Spring, -Across));
	// The soffit: one flat strip per arc segment, facing down into the opening.
	const FVector Mid = At(Cu, A.Spring, 0);
	for (int32 k = 0; k + 1 < Arc.Num(); ++k)
	{
		const FVector P0 = At(Arc[k].X, Arc[k].Y, 0), P1 = At(Arc[k + 1].X, Arc[k + 1].Y, 0);
		const FVector U = (P1 - P0).GetSafeNormal();
		FVector N = FVector::CrossProduct(U, Along).GetSafeNormal();
		if (FVector::DotProduct(N, Mid - 0.5 * (P0 + P1)) < 0.0)
		{
			N = -N;
		}
		AddFace(Face(P0, U, Along, (P1 - P0).Size(), D, N));
	}
}

int32 FPaintSurface::Build(const TArray<FPaintBox>& InBoxes, const TArray<FPaintArch>& Arches, const TArray<FPaintOpening>& Openings, UObject* Outer)
{
	Boxes = InBoxes;
	Faces.Reset();
	Texels.Reset();

	// The arches' solid parts first (proxies), so every face's burial and AO tests see them; then their faces.
	const int32 NumOwnBoxes = Boxes.Num();
	TArray<int32> Owners;
	Owners.SetNum(Arches.Num());
	for (int32 a = 0; a < Arches.Num(); ++a)
	{
		AddArch(Arches[a], false, Owners[a]);
	}
	for (int32 a = 0; a < Arches.Num(); ++a)
	{
		AddArch(Arches[a], true, Owners[a]);
	}

	// ---- faces: the six of every box, cut into texels; a face that is buried in other boxes everywhere is dropped.
	for (int32 b = 0; b < NumOwnBoxes; ++b)
	{
		const FPaintBox& B = Boxes[b];
		if (B.bProxy)
		{
			continue;
		}
		const FVector Size = B.Max - B.Min;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 AU = Axis == 0 ? 1 : 0;
			const int32 AV = Axis == 2 ? 1 : 2;
			for (const int32 Sign : { -1, 1 })
			{
				FFace F;
				F.Box = b;
				F.Skip = b;
				F.N = FVector::ZeroVector;
				F.N[Axis] = Sign;
				F.U = FVector::ZeroVector;
				F.U[AU] = 1.0;
				F.V = FVector::ZeroVector;
				F.V[AV] = 1.0;
				F.Origin = B.Min;
				F.Origin[Axis] = Sign > 0 ? B.Max[Axis] : B.Min[Axis];
				F.W = Size[AU];
				F.H = Size[AV];
				AddFace(F);
			}
		}
	}

	// ---- atlas: shelf packing, tallest tiles first, every tile with a gutter that repeats its border texels.
	TArray<int32> Order;
	for (int32 f = 0; f < Faces.Num(); ++f)
	{
		Order.Add(f);
	}
	Order.Sort([this](int32 A, int32 B) { return Faces[A].NV > Faces[B].NV; });
	AtlasW = 1024;
	int32 X = 0, Y = 0, Shelf = 0;
	for (const int32 f : Order)
	{
		FFace& F = Faces[f];
		const int32 TW = F.NU + 2 * Gutter;
		const int32 TH = F.NV + 2 * Gutter;
		if (X + TW > AtlasW)
		{
			X = 0;
			Y += Shelf;
			Shelf = 0;
		}
		F.AX = X + Gutter;
		F.AY = Y + Gutter;
		X += TW;
		Shelf = FMath::Max(Shelf, TH);
	}
	AtlasH = FMath::Max(4, Align(Y + Shelf, 4));

	// ---- bake: AO, openings and edges per texel.
	Values.SetNumZeroed(Texels.Num());
	ParallelFor(Faces.Num(), [this, &Openings](int32 f) { BakeFace(f, Openings); });
	VisibleTexels = 0;
	TargetArea = 0.0;
	TargetPainted = 0.0;
	Field.Init(0.0f, Texels.Num());
	Puddles.Reset();
	Growing.Reset();
	PuddleHash.Reset();
	MaxEdge = 0.0f;
	Hash.Reset();
	DirtyFaces.Reset();
	for (int32 t = 0; t < Texels.Num(); ++t)
	{
		const FTexel& T = Texels[t];
		if (T.bVisible)
		{
			++VisibleTexels;
			TargetArea += T.bTarget ? T.Area : 0.0;
			Hash.FindOrAdd(HashCell(FVector(T.P))).Add(t);
		}
	}

	Pixels.SetNumZeroed(AtlasW * AtlasH);
	for (int32 f = 0; f < Faces.Num(); ++f)
	{
		WriteTile(f);
	}

	Atlas = UTexture2D::CreateTransient(AtlasW, AtlasH, PF_B8G8R8A8, TEXT("PaintAtlas"));
	Atlas->Rename(nullptr, Outer);
	Atlas->SRGB = false;
	Atlas->Filter = TF_Bilinear;
	Atlas->AddressX = TA_Clamp;
	Atlas->AddressY = TA_Clamp;
	Atlas->NeverStream = true;
	FTexture2DMipMap& Mip = Atlas->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Atlas->UpdateResource();
	return Texels.Num();
}

void FPaintSurface::BakeFace(int32 FaceIndex, const TArray<FPaintOpening>& Openings)
{
	const FFace& F = Faces[FaceIndex];
	static const TArray<FVector> Rays = HemisphereRays(48);

	// Boxes within reach of this face's rays (its own box cannot be hit by rays leaving it).
	const FBox FaceBox = FBox(F.Origin, F.Origin + F.U * F.W + F.V * F.H).ExpandBy(AORadius);
	TArray<int32> Candidates;
	for (int32 k = 0; k < Boxes.Num(); ++k)
	{
		if (k != F.Skip && FaceBox.Intersect(FBox(Boxes[k].Min, Boxes[k].Max)))
		{
			Candidates.Add(k);
		}
	}
	const FVector T1 = F.U;
	const FVector T2 = FVector::CrossProduct(F.N, F.U);

	for (int32 j = 0; j < F.NV; ++j)
	{
		for (int32 i = 0; i < F.NU; ++i)
		{
			const int32 t = F.First + j * F.NU + i;
			const FTexel& T = Texels[t];
			FColor& Out = Values[t];
			if (!T.bVisible)
			{
				// Buried in another box: dark, like the corner it borders (bilinear filtering blends it in a little).
				Out = FColor(0, ToByte(0.5f), 0, 0);
				continue;
			}
			const FVector P = FVector(T.P);

			// Ambient occlusion: rays over the hemisphere, nearer hits darker.
			const FVector From = P + F.N * 0.25;
			float Broad = 0.0f, Contact = 0.0f;
			for (const FVector& L : Rays)
			{
				const FVector D = T1 * L.X + T2 * L.Y + F.N * L.Z;
				const float Hit = RayHit(From, D, AORadius, Candidates);
				Broad += Hit < AORadius ? 1.0f - Hit / AORadius : 0.0f;
				Contact += Hit < ContactRadius ? 1.0f - Hit / ContactRadius : 0.0f;
			}
			const float AO = 1.0f - FMath::Min(0.95f, AOStrength * FMath::Min(1.0f, 1.6f * Broad / Rays.Num()) + ContactStrength * FMath::Min(1.0f, 2.0f * Contact / Rays.Num()));

			// Openings on this face: a signed distance (negative inside), stored as 0.5 - d / (2 OpeningRange).
			float Opening = 0.0f;
			for (const FPaintOpening& O : Openings)
			{
				const FVector ON = O.Normal.GetSafeNormal();
				if (FVector::DotProduct(ON, F.N) < 0.99 || FMath::Abs(FVector::DotProduct(P - O.At, ON)) > 0.5)
				{
					continue;
				}
				const FVector Across = FVector::CrossProduct(FVector::UpVector, ON).GetSafeNormal();
				const double X = FVector::DotProduct(P - O.At, Across);
				const double Y = P.Z - O.At.Z;
				const double R = 0.5 * O.Width;
				double Dist;
				if (O.bRound)
				{
					Dist = FMath::Sqrt(X * X + (Y - R) * (Y - R)) - R;
				}
				else if (Y <= O.Height)
				{
					Dist = FMath::Max(FMath::Abs(X) - R, -Y);
				}
				else if (O.bPointed)
				{
					// Two circles of radius 2R through the opposite springing points meet in a point 1.73 R up.
					const double Yr = Y - O.Height;
					Dist = FMath::Max(FMath::Sqrt((X + R) * (X + R) + Yr * Yr), FMath::Sqrt((X - R) * (X - R) + Yr * Yr)) - 2.0 * R;
				}
				else
				{
					Dist = FMath::Sqrt(X * X + (Y - O.Height) * (Y - O.Height)) - R;
				}
				Opening = FMath::Max(Opening, FMath::Clamp(float(0.5 - Dist / (2.0 * OpeningRange)), 0.0f, 1.0f));
			}

			// Edge light: along top-face edges where the surface turns down (convex), 1 at the edge.
			float Edge = 0.0f;
			if (F.N.Z > 0.5 && F.W >= EdgeMinFace && F.H >= EdgeMinFace)
			{
				const double Du = (i + 0.5) * F.W / F.NU;
				const double Dv = (j + 0.5) * F.H / F.NV;
				const double Dist[4] = { Du, F.W - Du, Dv, F.H - Dv };
				const FVector Out4[4] = { -F.U, F.U, -F.V, F.V };
				for (int32 e = 0; e < 4; ++e)
				{
					if (Dist[e] < 3.0 * EdgeWidth && !Inside(P + Out4[e] * (Dist[e] + 0.5) - F.N * 0.5))
					{
						Edge = FMath::Max(Edge, float(1.0 - Dist[e] / (3.0 * EdgeWidth)));
					}
				}
			}
			Out = FColor(0, ToByte(AO), ToByte(Opening), ToByte(Edge));
		}
	}
}

void FPaintSurface::WriteTile(int32 FaceIndex)
{
	const FFace& F = Faces[FaceIndex];
	for (int32 j = -Gutter; j < F.NV + Gutter; ++j)
	{
		const int32 SJ = FMath::Clamp(j, 0, F.NV - 1);
		for (int32 i = -Gutter; i < F.NU + Gutter; ++i)
		{
			const int32 SI = FMath::Clamp(i, 0, F.NU - 1);
			const FColor& V = Values[F.First + SJ * F.NU + SI];
			// BGRA8 in memory: FColor stores B, G, R, A, so R = paint, G = AO, B = opening, A = edge.
			Pixels[(F.AY + j) * AtlasW + F.AX + i] = V;
		}
	}
}

void FPaintSurface::BuildMesh(UProceduralMeshComponent* Mesh, int32 NumPalettes, const TArray<FPaintSpire>& Spires, const TArray<FPaintRibbon>& Ribbons) const
{
	struct FSection
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;

		/** A flat triangle facing Facing (vertex colour: a shade the material multiplies in). */
		void Tri(const FVector& A, const FVector& B, const FVector& C, const FVector& Facing, const FVector2D& TA = FVector2D::ZeroVector,
			const FVector2D& TB = FVector2D::ZeroVector, const FVector2D& TC = FVector2D::ZeroVector, float Shade = 1.0f)
		{
			// Front faces: (C - A) x (B - A) along the normal.
			const FVector N0 = FVector::CrossProduct(C - A, B - A);
			const bool bFlip = FVector::DotProduct(N0, Facing) < 0.0;
			const FVector N = Facing.GetSafeNormal();
			const int32 Base = Vertices.Num();
			const FVector P[3] = { A, B, C };
			const FVector2D T[3] = { TA, TB, TC };
			for (int32 k = 0; k < 3; ++k)
			{
				Vertices.Add(P[k]);
				Normals.Add(N);
				UV.Add(T[k]);
				Colors.Add(FLinearColor(Shade, Shade, Shade, 1.0f));
				Tangents.Add(FProcMeshTangent((B - A).GetSafeNormal(), false));
			}
			Triangles.Append(bFlip ? std::initializer_list<int32>{ Base, Base + 2, Base + 1 } : std::initializer_list<int32>{ Base, Base + 1, Base + 2 });
		}
	};
	TArray<FSection> Sections;
	Sections.SetNum(2 * NumPalettes);

	for (const FFace& F : Faces)
	{
		FSection& S = Sections[FMath::Clamp(Boxes[F.Box].Palette, 0, NumPalettes - 1)];
		// Face coordinates (cm) to the mesh and to the face's tile in the atlas.
		auto Pos = [&F](const FVector2D& L) { return F.Origin + F.U * L.X + F.V * L.Y; };
		auto Tex = [&F, this](const FVector2D& L) { return FVector2D((F.AX + L.X / F.W * F.NU) / AtlasW, (F.AY + L.Y / F.H * F.NV) / AtlasH); };
		if (F.Poly.Num() > 0)
		{
			for (int32 k = 0; k + 2 < F.Poly.Num(); k += 3)
			{
				S.Tri(Pos(F.Poly[k]), Pos(F.Poly[k + 1]), Pos(F.Poly[k + 2]), F.N, Tex(F.Poly[k]), Tex(F.Poly[k + 1]), Tex(F.Poly[k + 2]));
			}
		}
		else
		{
			const FVector2D L[4] = { FVector2D(0, 0), FVector2D(F.W, 0), FVector2D(F.W, F.H), FVector2D(0, F.H) };
			S.Tri(Pos(L[0]), Pos(L[1]), Pos(L[2]), F.N, Tex(L[0]), Tex(L[1]), Tex(L[2]));
			S.Tri(Pos(L[0]), Pos(L[2]), Pos(L[3]), F.N, Tex(L[0]), Tex(L[2]), Tex(L[3]));
		}
	}

	// Spires: rings of a profile (a cone, or a teardrop), every facet flat, alternate facets a shade lighter.
	for (const FPaintSpire& Sp : Spires)
	{
		FSection& S = Sections[NumPalettes + FMath::Clamp(Sp.Palette, 0, NumPalettes - 1)];
		TArray<FVector2D> Profile; // (radius factor, height factor)
		if (Sp.Bulge > 0.0f)
		{
			// A teardrop: widest in the lower third, then a long concave taper to a needle.
			const float B = Sp.Bulge;
			Profile = { { 0.75f, 0.0f }, { 1.0f + 0.8f * B, 0.1f }, { 1.0f + B, 0.22f }, { 0.85f + 0.3f * B, 0.36f }, { 0.5f, 0.52f }, { 0.22f, 0.68f }, { 0.08f, 0.84f }, { 0.0f, 1.0f } };
		}
		else
		{
			Profile = { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
		}
		const int32 N = FMath::Max(3, Sp.Sides);
		auto Ring = [&Sp, N](const FVector2D& Pr, int32 k)
		{
			const double A = 2.0 * PI * (k + 0.5) / N;
			return Sp.Base + FVector(FMath::Cos(A) * Sp.Radius * Pr.X, FMath::Sin(A) * Sp.Radius * Pr.X, Sp.Height * Pr.Y);
		};
		for (int32 r = 0; r + 1 < Profile.Num(); ++r)
		{
			for (int32 k = 0; k < N; ++k)
			{
				const FVector A = Ring(Profile[r], k), B = Ring(Profile[r], k + 1);
				const FVector C = Ring(Profile[r + 1], k + 1), D = Ring(Profile[r + 1], k);
				const double Mid = 2.0 * PI * (k + 1.0) / N;
				const FVector Outward(FMath::Cos(Mid), FMath::Sin(Mid), 0.0);
				const float Shade = (k % 2) ? 1.18f : 1.0f;
				const FVector2D Z = FVector2D::ZeroVector;
				S.Tri(A, C, B, Outward, Z, Z, Z, Shade);
				if (Profile[r + 1].X > 0.0f)
				{
					S.Tri(A, D, C, Outward, Z, Z, Z, Shade);
				}
			}
		}
	}

	// Ribbons: a tapering strip that waves sideways and droops a little.
	for (const FPaintRibbon& Rb : Ribbons)
	{
		FSection& S = Sections[NumPalettes + FMath::Clamp(Rb.Palette, 0, NumPalettes - 1)];
		const FVector Dir = Rb.Direction.GetSafeNormal();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
		constexpr int32 Steps = 20;
		auto Edge = [&](int32 k, double Sign)
		{
			const double T = double(k) / Steps;
			const FVector C = Rb.Start + Dir * (T * Rb.Length) + Side * (Rb.Amplitude * FMath::Sin(2.0 * PI * Rb.Waves * T) * T) - FVector::UpVector * (0.12 * Rb.Length * T * T);
			return C + FVector::UpVector * (Sign * 0.5 * Rb.Height * (1.0 - 0.85 * T));
		};
		for (int32 k = 0; k < Steps; ++k)
		{
			const FVector A = Edge(k, -1), B = Edge(k + 1, -1), C = Edge(k + 1, 1), D = Edge(k, 1);
			const FVector N = FVector::CrossProduct(B - A, D - A);
			S.Tri(A, B, C, N);
			S.Tri(A, C, D, N);
		}
	}

	Mesh->ClearAllMeshSections();
	for (int32 s = 0; s < Sections.Num(); ++s)
	{
		const FSection& S = Sections[s];
		Mesh->CreateMeshSection_LinearColor(s, S.Vertices, S.Triangles, S.Normals, S.UV, S.Colors, S.Tangents, false);
	}
}

FIntVector FPaintSurface::HashCell(const FVector& P) const
{
	return FIntVector(FMath::FloorToInt(P.X / HashSize), FMath::FloorToInt(P.Y / HashSize), FMath::FloorToInt(P.Z / HashSize));
}

FIntVector FPaintSurface::PuddleCell(const FVector& P) const
{
	return FIntVector(FMath::FloorToInt(P.X / PuddleCellSize), FMath::FloorToInt(P.Y / PuddleCellSize), FMath::FloorToInt(P.Z / PuddleCellSize));
}

void FPaintSurface::AddPuddle(const FVector& At, float Radius, const FVector& Dir)
{
	FPuddle P;
	P.At = At;
	P.In = Dir.GetSafeNormal();
	P.Full = Radius / EdgeShare;
	P.bGrowing = true;
	const int32 Index = Puddles.Add(P);
	PuddleHash.FindOrAdd(PuddleCell(At)).Add(Index);
	Growing.Add(Index);
	MaxEdge = FMath::Max(MaxEdge, Radius);
}

bool FPaintSurface::Feed(const FVector& At, float Weight)
{
	// The puddle whose edge the drop lands deepest inside (distance over its current edge radius).
	int32 Best = INDEX_NONE;
	float BestShare = 1.05f;
	const FIntVector C0 = PuddleCell(At - FVector(MaxEdge)), C1 = PuddleCell(At + FVector(MaxEdge));
	for (int32 z = C0.Z; z <= C1.Z; ++z)
	{
		for (int32 y = C0.Y; y <= C1.Y; ++y)
		{
			for (int32 x = C0.X; x <= C1.X; ++x)
			{
				if (const TArray<int32>* Cell = PuddleHash.Find(FIntVector(x, y, z)))
				{
					for (const int32 i : *Cell)
					{
						const FPuddle& P = Puddles[i];
						const float Edge = FMath::Max(P.Now * EdgeShare, 0.5f);
						const float Share = float((At - P.At).Size()) / Edge;
						if (Share < BestShare)
						{
							BestShare = Share;
							Best = i;
						}
					}
				}
			}
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	Puddles[Best].Pending += Weight * FeedArea;
	return true;
}

void FPaintSurface::Tick(float DeltaSeconds)
{
	// Fed growth, in batches: the edge radius grows so the puddle's area grows by what it was fed.
	FeedClock += DeltaSeconds;
	if (FeedClock >= FeedPeriod)
	{
		FeedClock = 0.0f;
		for (int32 i = 0; i < Puddles.Num(); ++i)
		{
			FPuddle& P = Puddles[i];
			if (P.Pending <= 0.0f)
			{
				continue;
			}
			const float Edge = P.Full * EdgeShare;
			const float Grown = FMath::Sqrt(Edge * Edge + P.Pending / UE_PI);
			P.Full = Grown / EdgeShare;
			P.Pending = 0.0f;
			MaxEdge = FMath::Max(MaxEdge, Grown);
			if (!P.bGrowing)
			{
				P.bGrowing = true;
				Growing.Add(i);
			}
		}
	}
	// Spreading: the kernel radius approaches its size as 1 - exp(-t / SpreadTime), stamped every 0.25 cm.
	const float Step = 1.0f - FMath::Exp(-DeltaSeconds / SpreadTime);
	for (int32 g = Growing.Num() - 1; g >= 0; --g)
	{
		FPuddle& P = Puddles[Growing[g]];
		P.Shown += (P.Full - P.Shown) * Step;
		const bool bDone = P.Full - P.Shown < 0.25f;
		P.Shown = bDone ? P.Full : P.Shown;
		if (P.Shown > P.Now + 0.25f || bDone)
		{
			Spread(P.At, P.In, P.Now, P.Shown);
			P.Now = P.Shown;
		}
		if (bDone)
		{
			P.bGrowing = false;
			Growing.RemoveAtSwap(g);
		}
	}
}

void FPaintSurface::Spread(const FVector& At, const FVector& In, float From, float To)
{
	if (To <= From)
	{
		return;
	}
	auto Bump = [](float D, float R) { const float X = R > 0.0f ? D / R : 1.0f; return X < 1.0f ? FMath::Cube(1.0f - X * X) : 0.0f; };
	const FIntVector C0 = HashCell(At - FVector(To)), C1 = HashCell(At + FVector(To));
	for (int32 z = C0.Z; z <= C1.Z; ++z)
	{
		for (int32 y = C0.Y; y <= C1.Y; ++y)
		{
			for (int32 x = C0.X; x <= C1.X; ++x)
			{
				const TArray<int32>* Cell = Hash.Find(FIntVector(x, y, z));
				if (!Cell)
				{
					continue;
				}
				for (const int32 t : *Cell)
				{
					const FTexel& T = Texels[t];
					// Faces turned away from the rain stay dry (no paint through a wall's back).
					if (FVector::DotProduct(Faces[T.Face].N, In) > 0.35)
					{
						continue;
					}
					const float D = float((FVector(T.P) - At).Size());
					if (D >= To)
					{
						continue;
					}
					const float Before = Field[t];
					Field[t] += Bump(D, To) - Bump(D, From);
					if (Before < PaintLevel && Field[t] >= PaintLevel)
					{
						TargetPainted += T.bTarget ? T.Area : 0.0;
					}
					const uint8 V = ToByte(FMath::Min(1.0f, 0.5f * Field[t] / PaintLevel));
					if (V != Values[t].R)
					{
						Values[t].R = V;
						DirtyFaces.Add(T.Face);
					}
				}
			}
		}
	}
}

void FPaintSurface::ClearPaint()
{
	for (int32 t = 0; t < Values.Num(); ++t)
	{
		if (Values[t].R != 0)
		{
			Values[t].R = 0;
			DirtyFaces.Add(Texels[t].Face);
		}
	}
	Field.Init(0.0f, Texels.Num());
	Puddles.Reset();
	Growing.Reset();
	PuddleHash.Reset();
	MaxEdge = 0.0f;
	FeedClock = 0.0f;
	TargetPainted = 0.0;
}

void FPaintSurface::Flush()
{
	if (!Atlas || DirtyFaces.Num() == 0 || !Atlas->GetResource())
	{
		return;
	}
	// One region per dirty tile (with its gutter); the copy of the pixels goes with the update.
	TArray<FUpdateTextureRegion2D>* Regions = new TArray<FUpdateTextureRegion2D>();
	for (const int32 f : DirtyFaces)
	{
		WriteTile(f);
		const FFace& F = Faces[f];
		Regions->Add(FUpdateTextureRegion2D(F.AX - Gutter, F.AY - Gutter, F.AX - Gutter, F.AY - Gutter, F.NU + 2 * Gutter, F.NV + 2 * Gutter));
	}
	DirtyFaces.Reset();
	TArray<FColor>* Copy = new TArray<FColor>(Pixels);
	Atlas->UpdateTextureRegions(0, Regions->Num(), Regions->GetData(), AtlasW * sizeof(FColor), sizeof(FColor), reinterpret_cast<uint8*>(Copy->GetData()),
		[Regions, Copy](uint8*, const FUpdateTextureRegion2D*) { delete Regions; delete Copy; });
}

FBox FPaintSurface::Bounds() const
{
	FBox B(ForceInit);
	for (const FPaintBox& Box : Boxes)
	{
		B += FBox(Box.Min, Box.Max);
	}
	return B;
}
