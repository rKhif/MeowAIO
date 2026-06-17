#include "Zeri.h"

#include "ClearMode.h"
#include "collision.h"
#include "Farming.h"
#include "Utility.h"
#include "GameClient.h"
#include "Hooks.h"
#include "Orbwalker.h"

namespace Features::Champions::Zeri
{
#pragma region Helpers
	float StaticQCD = 1.f;

	void UpdateSpells()
	{
		// 750 + Bonus Attack Range
		Q.SetRange( 750 + ( ObjectManager::GetPlayer()->CharacterIntermediate()->mAttackRange.Value - 500 ) );

		// Half the delay instead of having to predict -> estimate cone width -> predict again for adjusted cast pos
		Q.CastDelay = ObjectManager::GetPlayer()->ComputeCharacterAttackCastDelay() / 2.f;

		// 1 / Attack Speed
		StaticQCD = 1 / Orbwalker::SDK::GetAttackSpeed( ObjectManager::GetPlayer() );

		const float ASRatio = min( (ObjectManager::GetPlayer()->CharacterIntermediate()->mAttackSpeedMod.Value - 1), 2.778f );
		const float Ratio = -0.09f * max( 0.f, ASRatio ) + 0.55f;

		W.CastDelay = Ratio;

		// Q can go through units
		if ( ObjectManager::GetPlayer()->BuffManager()->HasBuff( EBuff ) )
		{
			Q.HeroCollisionType = Soft;
			Q.MinionCollisionType = Soft;
			Q.TowerCollisionType = Soft;
		}
		else
		{
			Q.HeroCollisionType = Hard;
			Q.MinionCollisionType = Hard;
			Q.TowerCollisionType = Hard;
		}

		if ( ObjectManager::GetPlayer()->BuffManager()->HasBuff( RActiveBuff ) )
		{
			Q.Speed = 3400.f;
		}
		else
		{
			Q.Speed = 2600.f;
		}
	}

	struct WallJump
	{
		Vector3f Position;
		Vector3f CastPosition;
	};

	std::vector<WallJump> WallJumps;

	void InitializeWallJumps() // 524, 3964
	{
		const auto MapID = GameStartData::GetInstance()->Map();

		if ( MapID == GameMapId::SummonersRift )
		{
			WallJumps.emplace_back( WallJump{ Vector3f( 6543.56f, 48.53f, 4787.35f ), Vector3f( 6978.00f, 52.42f, 6238.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 6974.34f, 52.28f, 6278.47f ), Vector3f( 6548.00f, 48.53f, 4800.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 9522.00f, 64.96f, 3570.00f ), Vector3f( 8924.00f, -71.24f, 5478.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 8917.89f, -71.24f, 5511.04f ), Vector3f( 9522.00f, 64.96f, 3570.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 758.71f, 52.84f, 10776.69f ), Vector3f( 774.00f, 56.84f, 13306.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 8010.97f, 52.84f, 14177.41f ), Vector3f( 10572.00f, 91.43f, 14356.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 10738.70f, 52.31f, 9860.43f ), Vector3f( 12058.00f, 52.37f, 8152.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 12103.97f, 52.40f, 8136.85f ), Vector3f( 10736.00f, 52.45f, 9846.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 14141.02f, 52.31f, 7396.93f ), Vector3f( 14222.00f, 91.43f, 10506.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 13172.59f, 55.42f, 6133.08f ), Vector3f( 12140.00f, 52.08f, 7266.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 12109.34f, 52.08f, 7299.52f ), Vector3f( 13172.00f, 55.41f, 6108.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 14144.00f, 53.65f, 4394.00f ), Vector3f( 13888.00f, 48.21f, 1836.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 10666.00f, -70.32f, 5714.00f ), Vector3f( 10534.00f, 51.74f, 7456.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 10523.52f, 51.71f, 7502.02f ), Vector3f( 10666.00f, -70.32f, 5714.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 5252.25f, 56.82f, 11399.10f ), Vector3f( 5998.00f, -61.38f, 9414.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 10409.76f, 51.34f, 793.02f ), Vector3f( 13272.00f, 40.04f, 1058.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 5998.00f, -61.38f, 9414.00f ), Vector3f( 5250.00f, 56.81f, 11376.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 3918.00f, 52.84f, 14080.00f ), Vector3f( 1374.00f, 58.32f, 13906.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 7681.81f, 49.44f, 748.44f ), Vector3f( 4624.00f, 93.38f, 558.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 614.32f, 95.75f, 4501.11f ), Vector3f( 700.00f, 52.84f, 7556.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 709.20f, 52.84f, 7587.31f ), Vector3f( 624.00f, 95.75f, 4508.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 1656.46f, 52.84f, 8869.72f ), Vector3f( 2650.00f, 50.74f, 7524.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 2645.28f, 50.43f, 7488.25f ), Vector3f( 1672.00f, 52.81f, 8852.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 2314.00f, 48.63f, 11748.00f ), Vector3f( 3448.00f, 22.20f, 9354.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 3448.00f, 22.20f, 9354.00f ), Vector3f( 2314.00f, 48.63f, 11748.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 8221.36f, 49.55f, 10142.62f ), Vector3f( 7998.00f, 52.47f, 8686.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 7993.60f, 52.46f, 8672.59f ), Vector3f( 8214.00f, 49.50f, 10120.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 4209.63f, 50.62f, 7404.97f ), Vector3f( 4024.00f, -68.92f, 9432.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 4023.32f, -69.42f, 9464.10f ), Vector3f( 4216.00f, 50.63f, 7440.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 2777.15f, 56.12f, 6728.51f ), Vector3f( 4144.00f, 50.26f, 5064.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 4144.00f, 50.26f, 5064.00f ), Vector3f( 2770.00f, 56.29f, 6718.00f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 666.f, 182.f, 160.f ), Vector3f( 3874.f, 96.f, 562.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 14445.f, 171.f, 14078.f ), Vector3f( 14306.f, 91.f, 11025.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 242.773f, 184.f, 761.57f ), Vector3f( 524.f, 95.7f, 3758.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 14022.f, 171.f, 14506.f ), Vector3f( 10514.f, 91.f, 14362.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 13246.f, 41.f, 1048.f ), Vector3f( 10375.f, 51.f, 782.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 13872.f, 45.f, 1758.f ), Vector3f( 14258.f, 53.f, 5537.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 1374.f, 58.f, 13906.f ), Vector3f( 4354.f, 52.f, 14106.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 824.f, 57.f, 13356.f ), Vector3f( 740.f, 52.f, 10053.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 3874.f, 95.f, 558.f ), Vector3f( 1290.f, 133.f, 243.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 524.f, 95.f, 3958.f ), Vector3f( 146.f, 178.f, 786.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 11029.f, 91.f, 14381.f ), Vector3f( 13062.f, 93.f, 14582.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 14272.f, 91.f, 11156.f ), Vector3f( 14632.f, 171.f, 14543.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 6560.f, 52.f, 7257.f ), Vector3f( 5643.f, 51.f, 7908.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 4972.f, 23.f, 8386.f ), Vector3f( 5712.f, 51.f, 7879.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 7522.f, 52.f, 8056.f ), Vector3f( 7001.f, -71.f, 8542.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 6374.f, -59.f, 9106.f ), Vector3f( 7326.f, 19.f, 8328.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 8322.f, 52.f, 7358.f ), Vector3f( 9974.f, 43.f, 6558.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 9883.f, 27.f, 6496.f ), Vector3f( 8978.f, 53.f, 7015.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 7237.f, 52.f, 6675.f ), Vector3f( 8474.f, -49.f, 5741.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 8488.f, -55.f, 5753.f ), Vector3f( 7842.f, -73.f, 6223.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 4724.f, 107.f, 608.f ), Vector3f( 10631.f, 49.f, 853.f ) } );
			// WallJumps.emplace_back( WallJump{ Vector3f( 674.0f, 95.75f, 4708.0f ), Vector3f( 699.53f, 52.84f, 7742.20f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 14222.f, 91.f, 10456.f ), Vector3f( 14158.f, 52.f, 7817.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 10472.f, 91.f, 14306.f ), Vector3f( 7705.f, 52.f, 14162.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 12496.f, 51.f, 3226.f ), Vector3f( 11396.f, 7.8f, 5470.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 11396.f, 7.8f, 5470.f ), Vector3f( 12462.f, 51.f, 3266.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 7220.f, 56.47f, 11806.f ), Vector3f( 4538.f, 56.47f, 12124.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 4538.f, 56.47f, 12124.f ), Vector3f( 7224.f, 56.4f, 11856.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 10322.f, 49.2f, 2758.f ), Vector3f( 7504.f, 52.57f, 3140.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 7504.f, 52.57f, 3140.f ), Vector3f( 10376.f, 49.22f, 2802.f ) } );
		}
		/*else if ( MapID == GameMapId::HowlingAbyss )
		{
			WallJumps.emplace_back( WallJump{ Vector3f( 2429.f, -186.f, 4245.f ), Vector3f( 4921.f, -178.f, 6002.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 4079.f, -183.f, 2545.f ), Vector3f( 5964.f, -178.f, 4885.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 5729.f, -178.f, 4695.f ), Vector3f( 3576.f, -178.f, 1580.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 10277.f, -174.f, 8534.f ), Vector3f( 8278.f, -178.f, 7060.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 8777.f, -177.f, 10043.f ), Vector3f( 6671.f, -178.f, 7665.f ) } );
			WallJumps.emplace_back( WallJump{ Vector3f( 8127.f, -178.f, 7093.f ), Vector3f( 10511.f, -172.f, 8458.f ) } );
		}*/
	}

	bool IsLaserReady()
	{
		return ObjectManager::GetPlayer()->BuffManager()->HasBuff( LaserReadyBuff );
	}

	float GetAADamage( AIBaseClient* Client, float ExecuteOffset = 0.f )
	{
		const float ClientRealHealth = Client->GetRealHealth( false, true );
		const uint32_t PlayerLevel = ObjectManager::GetPlayer()->mLevelRef();

		if ( IsLaserReady() )
		{
			float RawDamage = 90.f + ( 110.f / 17.f ) * ( PlayerLevel - 1 ) * ( 0.7025f + 0.0175f * ( PlayerLevel - 1 ) );
			RawDamage += 1.1f * ObjectManager::GetPlayer()->TotalAbilityPower();
			const float HPRatio = ( 1.f + ( 14.f / 17.f ) * ( PlayerLevel - 1 ) * ( 0.7025f + 0.0175f * ( PlayerLevel - 1 ) ) ) / 100.f;
			RawDamage += ( HPRatio / 100 ) * Client->mHealth()->mMaxHP.Value;

			return Damage::CalculateMagicalDamage( ObjectManager::GetPlayer(), Client, RawDamage );
		}

		float RawDamage = 10.f + ( 15.f / 17.f ) * ( PlayerLevel - 1 ) * ( 0.7025f + 0.0175f * ( PlayerLevel - 1 ) );
		RawDamage += 0.03f * ObjectManager::GetPlayer()->TotalAbilityPower();
		const float ActualDamage = Damage::CalculateMagicalDamage( ObjectManager::GetPlayer(), Client, RawDamage );

		float HealthThreshold = 60.f + 90.f / 17.f * ( PlayerLevel - 1 );
		HealthThreshold += 0.18f * ObjectManager::GetPlayer()->TotalAbilityPower();

		if ( ClientRealHealth - ExecuteOffset <= HealthThreshold )
			return INT_MAX; // Execute

		return ActualDamage;
	}

	float GetQDamage( AIBaseClient* Client, bool IncludeE = false, bool Secondary = false )
	{
		const auto& Player = ObjectManager::GetPlayer();

		const Damage::RawDamage OnHit = Damage::GetOnHitDamage( Player, Client );

		float QDamage = QBase[ Q.Level() ] + QRatio[ Q.Level() ] * Player->TotalAttackDamage();
		float EDamage = 0.f;

		if ( ( BuffCache::HasBuff( Player, EBuff ) || IncludeE ) && !Secondary )
		{
			float RawEDamage = EBase[ E.Level() ] + EBADRatio * Player->TotalBonusAttackDamage() + EAPRatio * Player->TotalAbilityPower();
			RawEDamage *= 1.f + Player->CharacterIntermediate()->mCrit.Value * ECritRatio;
			EDamage = Damage::CalculateMagicalDamage( Player, Client, RawEDamage );
		}

		QDamage = Damage::CalculatePhysicalDamage( Player, Client, QDamage );

		if ( !Secondary )
		{
			QDamage += OnHit.GetTotalPostMitigation();
		}

		return QDamage + EDamage;
	}

	float GetWDamage( AIBaseClient* Client, const bool Beam = false )
	{
		float Damage = 0.f;

		Damage += WBase[ W.Level() ];
		Damage += WADRatio * ObjectManager::GetPlayer()->TotalAttackDamage();
		Damage += WAPRatio * ObjectManager::GetPlayer()->TotalAbilityPower();

		if ( Beam && ( Client->IsHero() || Client->IsMonster() ) )
		{
			const float CritRatio = 1 + ( ObjectManager::GetPlayer()->HasItem( Item::InfinityEdge ) || ObjectManager::GetPlayer()->HasItem( Item::EdgeofFinality ) ? .75f + .45f : .75f );
			Damage *= CritRatio;
		}

		return Damage::CalculatePhysicalDamage( ObjectManager::GetPlayer(), Client, Damage );
	}

	float GetRDamage( AIBaseClient* Client )
	{
		float Damage = 0.f;

		Damage += RBase[ R.Level() ];
		Damage += RBADRatio * ObjectManager::GetPlayer()->TotalBonusAttackDamage();
		Damage += RAPRatio * ObjectManager::GetPlayer()->TotalAbilityPower();

		return Damage::CalculateMagicalDamage( ObjectManager::GetPlayer(), Client, Damage );
	}

	// Not accurate, simply skinhash value lol
	float GetGoldValue( AIBaseClient* Client )
	{
		switch ( Client->BaseCharacterData()->SkinHash() )
		{
		case SkinHash::SRU_OrderMinionMelee:
		case SkinHash::SRU_ChaosMinionMelee:
		case SkinHash::HA_OrderMinionMelee:
		case SkinHash::HA_ChaosMinionMelee:
			return 21.f;
		case SkinHash::SRU_OrderMinionRanged:
		case SkinHash::SRU_ChaosMinionRanged:
		case SkinHash::HA_OrderMinionRanged:
		case SkinHash::HA_ChaosMinionRanged:
			return 14.f;
		case SkinHash::SRU_OrderMinionSiege:
		case SkinHash::SRU_ChaosMinionSiege:
		case SkinHash::HA_OrderMinionSiege:
		case SkinHash::HA_ChaosMinionSiege:
		case SkinHash::SRU_OrderMinionSuper:
		case SkinHash::SRU_ChaosMinionSuper:
		case SkinHash::HA_OrderMinionSuper:
		case SkinHash::HA_ChaosMinionSuper:
			return 90.f;
		case SkinHash::YellowTrinket:
		case SkinHash::SightWard:
			return 10.f;
		case SkinHash::BlueTrinket:
			return 15.f;
		case SkinHash::JammerDevice:
			return 30.f;
		default:
			break;
		}

		return 3.f;
	}

	Prediction::Hitchance GetHitchance( const MenuSDK::List* Menu )
	{
		if ( !Menu )
			return Prediction::DashingEnd; // This is to make sure the result is visible xd
		switch ( Menu->Value )
		{
		case 0:
			return Prediction::VeryLow;
		case 1:
			return Prediction::Low;
		case 2:
			return Prediction::Medium;
		case 3:
			return Prediction::High;
		case 4:
			return Prediction::VeryHigh;
		case 5:
			return Prediction::DashingMidAir;
		default:
			return Prediction::Medium;
		}
	}

	bool IsValidMinion( AIBaseClient* Minion )
	{
		return Minion && Minion->IsActive() && Minion->IsVisible() && Minion->IsEnemy() && Minion->IsTargetable() && !Minion->IsDead() && !Minion->mHealth()->ShouldBeDead() && Minion->TotalAttackDamage() > 2.f;
	}

	AIBaseClient* GetNextChainTarget( const Vector3f& TargetPosition, std::vector<std::uint32_t> BlacklistedIds )
	{
		AIBaseClient* Result = nullptr;
		float MinDist = 655.f;
		for ( const auto& Client : ManagerTemplates::AIBases() )
		{
			// Chain lightning will target closest visible client within 650 units
			//
			if ( !Client || !Client->IsActive() || std::ranges::find( BlacklistedIds, Client->NetworkID() ) != BlacklistedIds.end() || !Client->IsValidTarget( 650.f, true, TargetPosition ) )
				continue;

			const auto Distance = Client->Position().Distance2D( TargetPosition );
			if ( Distance < MinDist )
			{
				MinDist = Distance;
				Result = Client;
			}
		}

		return Result;
	}

	Riot::Vector3f FindChainStart( AIBaseClient* DesiredTarget )
	{
		const auto PlayerPosition = ObjectManager::GetPlayer()->Position();
		for ( const auto& Client : ManagerTemplates::AIBases() )
		{
			// Ignore units we can not Q
			//
			if ( !Client || !Client->IsActive() || !Client->IsValidTarget( Q.Range, true, PlayerPosition ) )
				continue;

			Prediction::PredictionResult Result;
			if ( !PredictionSuccess( Prediction::GetPrediction(Client, Q, PlayerPosition, PlayerPosition, false, &Result) ) )
				continue;

			// Check the chain
			//
			AIBaseClient* CurrentTarget = Client;
			std::vector<uint32_t> Chain = { CurrentTarget->NetworkID() };
			for ( int i = 0; i < 4; i++ )
			{
				CurrentTarget = GetNextChainTarget( CurrentTarget->Position(), Chain );

				// Chain ends before it could reach our target
				//
				if ( !CurrentTarget )
					break;

				Chain.push_back( CurrentTarget->NetworkID() );

				// Chain leads to our target
				//
				if ( CurrentTarget->NetworkID() == DesiredTarget->NetworkID() )
				{
					return Result.CastPosition;
				}
			}
		}

		return Vector3f::Zero;
	}
#pragma endregion

#pragma region Orbwalker

	void OnAfterQ( AIBaseClient* Target )
	{
	}


	namespace ZeriWalker
	{
		enum class CurrentStatus : uint32_t
		{
			Inactive,
			Killing,
			SettingUp,
			Waiting
		};

		AIBaseClient* ComboTarget = nullptr;

		void CastQ( const Vector3f& CastPosition, AIBaseClient* Target )
		{
			if ( Q.CastSpell( CastPosition ) )
			{
				ComboTarget = Target;
				LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
			}
		}

		void Combo()
		{
			if ( !Q.IsReady() || !Menu::ComboQ->Value || !Orbwalker::SDK::CanAction() )
				return;

			const auto Player = ObjectManager::GetPlayer();
			const Vector3f PlayerPosition = Player->Position();

			for ( const auto& Target : TS->GetOrderedTargets() )
			{
				if ( TargetSelector::IsTargetInvalid( Target ) )
					continue;

				Prediction::PredictionResult Result;
				const auto Status = GetPrediction( Target, Q, PlayerPosition, PlayerPosition, false, &Result );

				if ( Status == Prediction::Unsuccessful )
					continue;

				const auto PredictionValidity = TS->IsValidPredResult( Target, Result );
				if ( !VALID_PREDICTION( PredictionValidity ) )
				{
					continue;
				}

				if ( Status == Prediction::Collision && Menu::ComboQIgnoreCollision->Value || Status == Prediction::Success )
				{
					CastQ( Result.CastPosition, Target );
					return;
				}

				if ( Status == Prediction::OutOfRange && Q.InRange( Target ) )
				{
					CastQ( Result.CastPosition, Target );
					return;
				}

				if ( Menu::ChainLogic->Value )
				{
					const auto Buff = BuffCache::GetBuff( Player, RActiveBuff );
					if ( !Buff || !Buff->IsActive() )
						return;

					const auto ChainLightingTarget = FindChainStart( Target );
					const auto TimeToHit = PlayerPosition.Distance2D( ChainLightingTarget ) / Q.Speed + 0.25f;

					if ( Menu::ChainLogicOnlyKeepAlive->Value && Buff->RemainingTime() + 0.2f > TimeToHit )
						return;

					if ( !ChainLightingTarget.IsNonZeroValid() )
						continue;

					CastQ( ChainLightingTarget, Target );
					return;
				}
			}
		}

		void Harass()
		{
		}

		std::uint32_t IgnoreTargetId = 0;

		void Clear()
		{
			if ( !Q.IsReady() || GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 0 ] < 0.1f )
				return;

			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

			if ( ObjectManager::GetPlayer()->HasBuffOfType( BuffType::Blind ) || !Orbwalker::SDK::CanAction() )
				return;

			for ( auto& Building : ManagerTemplates::Buildings() )
			{
				if ( Building->IsInhib() )
				{
					auto inhib = ( AttackableUnit* )Building;
					if ( inhib->IsValidTarget( FLT_MAX, true, Riot::Vector3f::Zero ) )
					{
						if ( inhib->Position().DistanceSquared( PlayerPosition ) <= Q.RangeSquared + ( 200.f * 200.f ) )
						{
							LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
							Q.CastSpell( inhib->Position() );
							return;
						}
					}
				}
				if ( Building->IsNexus() )
				{
					auto nexus = ( AttackableUnit* )Building;
					if ( nexus->IsValidTarget( FLT_MAX, true, Riot::Vector3f::Zero ) )
					{
						if ( nexus->Position().DistanceSquared( PlayerPosition ) <= Q.RangeSquared + ( 300.f * 300.f ) )
						{
							LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
							Q.CastSpell( nexus->Position() );
							return;
						}
					}
				}
			}

			auto LasthitData = Orbwalker::Farming::GetLastHitDataForSpell( Q, [&] ( AIBaseClient* Unit ) -> float
			                                                               {
				                                                               return GetQDamage( Unit );
			                                                               }, Orbwalker::TargetingContext{ PlayerPosition, MultiplayerClient::GetInstance()->CurrentRoundTripTime() },
			                                                               [] ( AIBaseClient* lhs, AIBaseClient* rhs ) -> bool
			                                                               {
				                                                               if ( std::fabsf( lhs->mHealth()->mMaxHP.Value - rhs->mHealth()->mMaxHP.Value ) < FLT_EPSILON )
				                                                               {
					                                                               return lhs->mHealth()->mHP.Value < rhs->mHealth()->mHP.Value;
				                                                               }

				                                                               return lhs->mHealth()->mMaxHP.Value > rhs->mHealth()->mMaxHP.Value;
			                                                               } );

			std::vector<AIBaseClient*> waitingFor = {};
			std::vector<AIBaseClient*> mustLasthit = {};
			std::vector<AIBaseClient*> canLasthit = {};
			std::vector<std::pair<AIBaseClient*, int>> canOrb = {};
			std::vector<AIBaseClient*> jungle = {};
			std::vector<AIBaseClient*> plants = {};
			std::vector<AIBaseClient*> wards = {};
			std::vector<AIBaseClient*> misc = {};

			bool WaitForMinion = false;

			for ( const auto& Minion : ManagerTemplates::Minions() )
			{
				if ( !Minion || !Minion->IsValidTarget( Q.Range, true, PlayerPosition ) )
					continue;

				auto hash = Minion->SkinHash();

				if ( hash == SkinHash::CherryPlantPowerup )
				{
					if ( !Minion->ShouldDrawHealthBar() )
						continue;

					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					Q.CastSpell( Minion->Position() );
					return;
				}

				if ( hash == SkinHash::GangplankBarrel )
				{
					if ( const auto owner = Minion->GetGoldRedirectTarget() )
					{
						if ( owner->IsEnemy() )
						{
							LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
							Q.CastSpell( Minion->Position() );
							return;
						}
					}

					continue;
				}

				if ( hash == SkinHash::ZacBloblet )
				{
					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					Q.CastSpell( Minion->Position() );
					return;
				}

				if ( Minion->IsPet() )
				{
					misc.push_back( Minion );
					continue;
				}

				if ( Minion->IsPlant() )
				{
					plants.push_back( Minion );
					continue;
				}

				if ( Minion->IsMonster() || hash == SkinHash::SRU_RiftHerald_Deployed )
				{
					jungle.push_back( Minion );
					continue;
				}

				if ( Minion->IsWard() )
				{
					wards.push_back( Minion );
					continue;
				}

				if ( !Orbwalker::Farming::IsMinionTargetValidForSpell( Minion, Q.Range ) )
					continue;

				if ( !LasthitData.CastPositionMap.contains( Minion->NetworkID() ) )
					continue;

				const bool IsKillable = std::ranges::find( LasthitData.KillableMinions, Minion ) != LasthitData.KillableMinions.end();
				const float TravelTime = LasthitData.CastPositionMap[ Minion->NetworkID() ].Distance2D( PlayerPosition ) / Q.Speed;
				auto HealthNextQ = HealthPrediction::GetHealthPrediction( Minion, 0.050f + StaticQCD + TravelTime );
				auto dmg = GetQDamage( Minion );

				if ( HealthNextQ < Minion->mHealth()->mHP.Value )
				{
					if ( HealthNextQ <= 1 && IsKillable )
					{
						mustLasthit.push_back( Minion );
					}
					else if ( IsKillable || Minion->GetRealHealth( true, true ) <= dmg )
					{
						canLasthit.push_back( Minion );
					}
					else if ( HealthNextQ - dmg > 0 )
					{
						canOrb.push_back( { Minion, LasthitData.CollisionCountMap[ Minion->NetworkID() ] } );
					}

					if ( HealthNextQ < 5 /*|| LasthitData.ForceWaitMinionMap[ unit->NetworkID() ]*/ )
					{
						waitingFor.push_back( Minion );
						WaitForMinion = true;
					}
				}
				else
				{
					if ( IsKillable || Minion->GetRealHealth( true, true ) <= dmg )
					{
						if ( Minion->mHealth()->mMaxHP.Value >= 700.f )
						{
							mustLasthit.push_back( Minion );
						}
						else
						{
							canLasthit.push_back( Minion );
						}
					}
					else if ( HealthNextQ - dmg > 0 )
					{
						canOrb.push_back( { Minion, LasthitData.CollisionCountMap[ Minion->NetworkID() ] } );
					}
				}
			}

			if ( !plants.empty() && Menu::LaneQPlant->Value )
			{
				LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
				Q.CastSpell( plants.front()->Position() );
				return;
			}

			auto SortMaxHealth = [] ( AIBaseClient* a, AIBaseClient* b )
			{
				return a->mHealth()->mMaxHP.Value > b->mHealth()->mMaxHP.Value;
			};

			auto SortHealth = [] ( std::pair<AIBaseClient*, int> rhs, std::pair<AIBaseClient*, int> lhs )
			{
				return rhs.first->mHealth()->mHP.Value < lhs.first->mHealth()->mHP.Value;
			};

			auto SortCollision = [] ( std::pair<AIBaseClient*, int> rhs, std::pair<AIBaseClient*, int> lhs )
			{
				return rhs.second > lhs.second;
			};

			std::ranges::sort( mustLasthit, SortMaxHealth );
			std::ranges::sort( waitingFor, SortMaxHealth );

			if ( !mustLasthit.empty() && mustLasthit.front() )
			{
				const auto Candidate = mustLasthit.front();
				if ( WaitForMinion )
				{
					if ( std::fabsf( Candidate->mHealth()->mMaxHP.Value - waitingFor.front()->mHealth()->mMaxHP.Value ) < 0.01f )
					{
						WaitForMinion = false;
						IgnoreTargetId = Candidate->NetworkID();
						LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
						Q.CastSpell( LasthitData.CastPositionMap[ Candidate->NetworkID() ] );
						return;
					}
				}
				else
				{
					IgnoreTargetId = Candidate->NetworkID();
					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					Q.CastSpell( LasthitData.CastPositionMap[ Candidate->NetworkID() ] );
					return;
				}
			}

			std::sort( canLasthit.begin(), canLasthit.end(), SortMaxHealth );

			if ( !canLasthit.empty() && canLasthit.front() )
			{
				const auto Candidate = canLasthit.front();

				if ( WaitForMinion )
				{
					if ( Candidate->Handle() == waitingFor.front()->Handle() )
					{
						IgnoreTargetId = Candidate->NetworkID();
						LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
						Q.CastSpell( LasthitData.CastPositionMap[ Candidate->NetworkID() ] );
						return;
					}
				}
				else
				{
					IgnoreTargetId = Candidate->NetworkID();
					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					Q.CastSpell( LasthitData.CastPositionMap[ Candidate->NetworkID() ] );
					return;
				}
			}

			for ( const auto& object : misc )
			{
				if ( object && !object->IsDead() && object->IsVisible() && object->ShouldDrawHealthBar() && object->IsValidTarget( Q.Range, true, PlayerPosition ) )
				{
					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					Q.CastSpell( object->Position() );
					return;
				}
			}

			for ( const auto& ward : wards )
			{
				if ( ward && !ward->IsDead() && ward->IsVisible() && ward->ShouldDrawHealthBar() && ward->IsValidTarget( Q.Range, true, PlayerPosition ) )
				{
					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					Q.CastSpell( ward->Position() );
					return;
				}
			}

			for ( auto& turret : ManagerTemplates::Turrets() )
			{
				if ( turret && turret->IsValidTarget( Q.Range, true, PlayerPosition ) )
				{
					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					Q.CastSpell( turret->Position() );
					return;
				}
			}

			std::sort( jungle.begin(), jungle.end(), [&] ( AIBaseClient* a, AIBaseClient* b )
			{
				if ( a->mHealth()->mMaxHP.Value > b->mHealth()->mMaxHP.Value )
					return true;
				if ( b->mHealth()->mMaxHP.Value > a->mHealth()->mMaxHP.Value )
					return false;

				if ( a->mHealth()->mHP.Value < b->mHealth()->mHP.Value )
					return true;
				if ( b->mHealth()->mHP.Value < a->mHealth()->mHP.Value )
					return false;

				return false;
			} );

			if ( Menu::JungleQ )
			{
				for ( auto& monster : jungle )
				{
					Prediction::PredictionResult Result;
					if ( !PredictionSuccess( Prediction::GetPrediction(monster, Q, PlayerPosition, PlayerPosition, true, &Result) ) )
						continue;

					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					Q.CastSpell( Result.CastPosition );
					return;
				}
			}

			if ( BuffCache::HasBuff( ObjectManager::GetPlayer(), EBuff ) )
			{
				std::ranges::sort( canOrb, SortCollision );
			}
			else
			{
				std::ranges::sort( canOrb, SortHealth );
			}


			if ( !WaitForMinion && !canOrb.empty() )
			{
				LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
				Q.CastSpell( LasthitData.CastPositionMap[ canOrb.front().first->NetworkID() ] );
			}
		}

		bool Lasthit()
		{
			if ( !Q.IsReady() || GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 0 ] < 0.1f )
				return false;

			auto Player = ObjectManager::GetPlayer();
			const auto& PlayerPosition = Player->Position();

			if ( Player->HasBuffOfType( BuffType::Blind ) || !Orbwalker::SDK::CanAction() )
				return false;

			auto SortingFunction = [] ( AIBaseClient* lhs, AIBaseClient* rhs ) -> bool
			{
				if ( std::fabsf( lhs->mHealth()->mMaxHP.Value - rhs->mHealth()->mMaxHP.Value ) < FLT_EPSILON )
				{
					return lhs->mHealth()->mHP.Value < rhs->mHealth()->mHP.Value;
				}

				return lhs->mHealth()->mMaxHP.Value > rhs->mHealth()->mMaxHP.Value;
			};

			auto LasthitData = Orbwalker::Farming::GetLastHitDataForSpell( Q, [&] ( AIBaseClient* Unit ) -> float
			                                                               {
				                                                               return GetQDamage( Unit );
			                                                               }, Orbwalker::TargetingContext{ PlayerPosition, MultiplayerClient::GetInstance()->CurrentRoundTripTime() },
			                                                               SortingFunction );

			if ( !LasthitData.KillableMinions.empty() && LasthitData.KillableMinionHealthBeforeNextAA[ LasthitData.KillableMinions.front()->NetworkID() ] > 1.f
				&& !LasthitData.SoonKillableMinionsThatWillDieBeforeNextAA.empty()
				&& !SortingFunction( LasthitData.KillableMinions.front(), LasthitData.SoonKillableMinionsThatWillDieBeforeNextAA.front() ) )
			{
				return true;
			}

			if ( !LasthitData.KillableMinions.empty() )
			{
				const auto Unit = LasthitData.KillableMinions.front();
				IgnoreTargetId = Unit->NetworkID();
				LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
				Q.CastSpell( LasthitData.CastPositionMap[ Unit->NetworkID() ] );
				return true;
			}

			return false;
		}
	}


#pragma endregion

	bool FindWall( Vector3f Start, Vector3f End, OUT Vector3f* WallPosition )
	{
		const auto Direction = ( End - Start ).Normalized();
		const auto Distance = Start.Distance2D( End );

		for ( float Range = 0.f; Range < Distance; )
		{
			const auto Position = Start + Direction * Range;

			if ( NavMesh::IsWall( Position ) )
			{
				*WallPosition = Position;
				return true;
			}

			Range = std::fminf( Range + 10.f, Distance );
		}

		return false;
	}

	bool WMissileLogic( AIBaseClient* Target, Prediction::Hitchance Hitchance, bool OnlyKillable, Vector3f SourcePosition )
	{
		if ( OnlyKillable && Target->GetRealHealth( true ) > GetWDamage( Target ) )
			return false;

		Prediction::PredictionResult Result;
		if ( !PredictionSuccess( Prediction::GetPrediction( Target, W, SourcePosition, SourcePosition, true, &Result ) ) )
			return false;

		if ( Result.Hitchance < Hitchance )
			return false;

		return W.CastSpell( Result.CastPosition );
	}

	bool WWallLogic( AIBaseClient* Target, Prediction::Hitchance Hitchance, bool OnlyKillable, Vector3f SourcePosition )
	{
		if ( NavMesh::IsWall( ObjectManager::GetPlayer()->Position() ) )
			Hitchance = Prediction::Medium;

		const float EstimatedTimeToWall = 300 / W.Speed;

		Prediction::PredictionData Data;
		GetPositionAfterTime( Target, W.CastDelay + EstimatedTimeToWall, &Data );
		const float DistanceToTarget = SourcePosition.Distance2D( Data.Position );
		const float FirstPhaseRange = std::fminf( DistanceToTarget, W.Range );
		const Vector3f FirstPhaseEndPosition = SourcePosition.Extend( Data.Position, FirstPhaseRange );

		Vector3f WallPosition;
		if ( !FindWall( SourcePosition, FirstPhaseEndPosition, &WallPosition ) )
			return false;

		const float DistanceToWall = SourcePosition.Distance2D( WallPosition );
		const float CalculatedWRange = DistanceToWall + 1500.f;
		const float TimeToWall = DistanceToWall / W.Speed;
		const float TotalTimeToWall = W.CastDelay + TimeToWall;
		const float TotalTimeToTarget = TotalTimeToWall + 0.85f;

		WBeam.SetRange( CalculatedWRange );
		WBeam.CastDelay = TotalTimeToTarget;

		if ( OnlyKillable && Target->GetRealHealth( true ) > GetWDamage( Target, true ) )
			return false;

		Prediction::PredictionResult Result;
		if ( !PredictionSuccess( Prediction::GetPrediction( Target, WBeam, SourcePosition, SourcePosition, true, &Result ) ) )
			return false;

		if ( Result.Hitchance < Hitchance )
			return false;

		const float PredictedDistanceToTarget = SourcePosition.Distance2D( Result.CastPosition );
		const float PredictedFirstPhaseRange = std::fminf( PredictedDistanceToTarget, W.Range );
		const Vector3f PredictedFirstPhaseEndPosition = SourcePosition.Extend( Result.CastPosition, PredictedFirstPhaseRange );

		Vector3f PredictedWallPosition;
		if ( !FindWall( SourcePosition, PredictedFirstPhaseEndPosition, &PredictedWallPosition ) )
			return false;

		if ( !PredictionSuccess( Prediction::FindSpellCollisions(W, SourcePosition, SourcePosition, PredictedWallPosition, Target, nullptr) ) )
			return false;

		return W.CastSpell( Result.CastPosition );
	}

	bool ComboW( bool CheckMana, bool Missile, bool Beam )
	{
		if ( !W.IsReady() || !Orbwalker::SDK::CanAction() )
			return false;

		if ( CheckMana )
		{
			const auto Mana = ObjectManager::GetPlayer()->mResource()->mPAR.Value;
			const auto WCost = CostW[ W.Level() ];
			const auto ECost = CostE[ E.Level() ];

			if ( Mana - WCost < ECost )
			{
				return false;
			}
		}

		const auto SourcePosition = ObjectManager::GetPlayer()->Position();

		for ( const auto& Target : TS->GetOrderedTargets() )
		{
			if ( !TargetSelector::IsTargetValidNonSkillshot( Target ) ) //TODO
				continue;

			if ( Menu::OnlyOutOfRangeW->Value && Target->Position().DistanceSquared2D( SourcePosition ) < Q.RangeSquared && !NavMesh::GetInstance()->IsWall( SourcePosition ) )
				continue;

			if ( Beam && WWallLogic( Target, GetHitchance( Menu::HitchanceBeam ), false, SourcePosition ) )
				return true;

			if ( Missile && WMissileLogic( Target, GetHitchance( Menu::HitchanceW ), false, SourcePosition ) )
				return true;
		}

		return false;
	}

	bool KillstealW()
	{
		if ( !W.IsReady() || !Orbwalker::SDK::CanAction() || !Menu::KillstealW->Value )
			return false;

		const auto SourcePosition = ObjectManager::GetPlayer()->Position();

		for ( const auto& Target : TS->GetOrderedTargets() )
		{
			if ( !TargetSelector::IsTargetValidNonSkillshot( Target ) ) //TODO
				continue;

			if ( Target->Position().DistanceSquared2D( SourcePosition ) < Q.RangeSquared )
				continue;

			if ( WWallLogic( Target, GetHitchance( Menu::HitchanceBeam ), true, SourcePosition ) )
				return true;

			if ( WMissileLogic( Target, GetHitchance( Menu::HitchanceW ), true, SourcePosition ) )
				return true;
		}

		return false;
	}

	//TODO maybe make safety range a menu value idk cba rn
	uint32_t GetEPositionScore( Vector3f Position )
	{
		uint32_t Score = 0;
		const auto PlayerPosition = ObjectManager::GetPlayer()->Position();
		const auto MaxDistance = E.Range + 50.f;

		for ( float i = 0.f; i <= MaxDistance; i += NavMesh::GetInstance()->CellWidth() )
		{
			if ( i > MaxDistance )
				break;

			const auto CurrentPosition = PlayerPosition.Extend( Position, i );
			if ( NavMesh::IsWall( CurrentPosition ) )
			{
				Score = 5000;
				break;
			}
		}

		const auto& [ Safe, Spells ] = LuaEnvironment::Exports::IsPathSafe( { ObjectManager::GetPlayer()->Position(), Position },
		                                                                    600.f + ObjectManager::GetPlayer()->CharacterIntermediate()->MoveSpeed() );

		if ( !Safe )
			return 5000;

		Score += EnemyHeroTargetsInRange( Position, 450.f * 450.f );
		return Score;
	}

	bool ComboE( bool Reset = false )
	{
		if ( !E.IsReady() || ( !Orbwalker::SDK::CanAction() && !Reset ) )
			return false;

		const auto Player = ObjectManager::GetPlayer();
		const auto PlayerPosition = Player->Position();

		const bool ShouldCast = Reset || !Menu::OnlyReset->Value;

		auto [ Target, Prediction ] = TS->GetBestTarget( Q, PlayerPosition, PlayerPosition, nullptr, [&] ( AIBaseClient* Unit, Prediction::PredictionStatus Status, Prediction::PredictionResult* PredictionQ ) -> bool
		{
			if ( Status == Prediction::PredictionStatus::Unsuccessful )
				return false;

			if ( Status == Prediction::PredictionStatus::OutOfRange )
			{
				//TODO gapclose
				return false;
			}

			if ( Menu::EMode->Value == 1 )
			{
				Prediction::PredictionData EPrediction;
				Prediction::GetPositionAfterTime( Unit, 300.f / ( 600.f + Player->CharacterIntermediate()->MoveSpeed() ), &EPrediction );

				auto [ P1, P2 ] = Collision::CircleCircle( PlayerPosition, EPrediction.Position, Q.Range, Q.Range );

				if ( P1.IsNonZeroValid() && P2.IsNonZeroValid() )
				{
					int ScoreP1 = GetEPositionScore( P1 );
					int ScoreP2 = GetEPositionScore( P2 );

					const std::pair<int, Vector3f> CastPosition = ScoreP1 < ScoreP2 ? std::make_pair( ScoreP1, P1 ) : std::make_pair( ScoreP2, P2 );

					if ( CastPosition.first > Menu::EnemyLimitE->Value )
					{
						return false;
					}

					PredictionQ->CastPosition = CastPosition.second;
				}
			}
			else
			{
				PredictionQ->CastPosition = HudManager::GetInstance()->CursorTargetLogic()->CursorPosition();
			}

			if ( Status == Prediction::PredictionStatus::Collision && Menu::ComboE->Values[ 1 ] )
			{
				//TODO logic if its worth to use?
				return ShouldCast || Menu::CollisionIgnoreReset->Value;
			}

			if ( Menu::ComboE->Values[ 0 ] && ShouldCast )
			{
				// Reset Q CD for 1 extra Q
				//
				const float ResetCastCount = Reset ? 4.f : 3.f;

				// Consider 3 Q's
				//
				const auto QEDamage = GetQDamage( Unit, true, Status == Prediction::PredictionStatus::Collision ) * ResetCastCount;
				const auto QDamage = Status != Prediction::PredictionStatus::Collision ? GetQDamage( Unit ) * 3.f : 0.f;
				const auto Health = Unit->GetRealHealth();

				if ( QDamage < Health && QEDamage >= Health )
				{
					return true;
				}
			}

			return false;
		} );

		if ( !Target )
			return false;

		return E.CastSpell( Prediction.CastPosition );
	}

	bool ComboR()
	{
		if ( !R.IsReady() || !Orbwalker::SDK::CanAction() )
			return false;

		if ( ObjectManager::GetPlayer()->mResource()->mPAR.Value - CostR <= CostE[ E.Level() ] && Menu::SaveManaE->Value )
			return false;

		std::uint32_t Count = 0;
		const auto PlayerPosition = ObjectManager::GetPlayer()->Position();

		for ( const auto& Target : TS->GetOrderedTargets() )
		{
			if ( !TargetSelector::IsTargetValidNonSkillshot( Target ) ) //TODO not this so it handles stasis and such
				continue;

			if ( Target->Position().Distance2D( PlayerPosition ) > R.Range )
				continue;

			Count++;

			// Continue early if killable is disabled
			//
			if ( !Menu::ComboR->Values[ 0 ] )
				continue;

			float QDamage = 0.f;
			Prediction::PredictionResult QPrediction;
			auto Status = Prediction::GetPrediction( Target, Q, PlayerPosition, PlayerPosition, false, &QPrediction );

			if ( PredictionSuccess( Status ) )
			{
				QDamage = GetQDamage( Target );
			}

			float AADamage = 0.f;
			if ( ObjectManager::GetPlayer()->IsInAutoAttackRange( Target ) )
			{
				AADamage += Damage::CalculateAutoAttackDamage( ObjectManager::GetPlayer(), Target );
			}

			const float WDamage = W.IsReady() || W.CooldownTimeRemaining() < 0.5f ? GetWDamage( Target ) : 0.f;
			const float Health = Target->GetRealHealth();
			const float MagicShield = Target->mHealth()->mMagicalShield.Value;
			const float PhysShield = Target->mHealth()->mPhysicalShield.Value;
			AADamage = std::fmaxf( AADamage - MagicShield, 0.f );

			if ( Health + MagicShield < GetRDamage( Target ) && Health + PhysShield > QDamage + WDamage + AADamage )
			{
				return R.CastSpell();
			}
		}

		if ( Count >= Menu::RCount->Value && Menu::ComboR->Values[ 1 ] )
		{
			return R.CastSpell();
		}

		return false;
	}

	void HandleJump()
	{
		if ( !Menu::JumpE->Value || !E.IsReady() )
			return;

		const auto PlayerPosition = ObjectManager::GetPlayer()->Position();
		const auto CursorPos = HudManager::GetInstance()->CursorTargetLogic()->CursorPosition();
		float MinDist = 1000.f;
		float DistToPoint = FLT_MAX;
		WallJump BestJump{ Vector3f::Zero, Vector3f::Zero };

		for ( const auto& Jump : WallJumps )
		{
			const float Distance = PlayerPosition.Distance2D( Jump.Position );
			const float CursorDistance = CursorPos.Distance2D( Jump.Position );

			if ( CursorDistance < MinDist )
			{
				MinDist = CursorDistance;
				DistToPoint = Distance;
				BestJump = Jump;
			}
		}

		if ( BestJump.Position.IsNonZeroValid() )
		{
			if ( DistToPoint < 15.f )
			{
				if ( E.CastSpell( BestJump.CastPosition ) )
					return;
			}

			const float PathEndDistance = ObjectManager::GetPlayer()->GetPathController()->GetNavigationPath()->EndPosition.Distance2D( BestJump.Position );

			if ( !ObjectManager::GetPlayer()->GetPathController()->IsMoving() || PathEndDistance > 15.f )
			{
				HudManager::GetInstance()->CursorTargetLogic()->OnRightClickMove( BestJump.Position, false, false, false, false, true, false );
				return;
			}
		}
	}

#pragma region Events

	void OnGameUpdate()
	{
		UpdateSpells();
		HandleJump();

		if ( KillstealW() )
			return;

		if ( Menu::SemiAutoW->Value )
		{
			ComboW( false, true, true );
		}

		switch ( Orbwalker::SDK::GetMode() )
		{
		case Orbwalker::Mode::Combo:
			{
				ZeriWalker::Combo();
				ComboR();
				ComboE();
				ComboW( true, Menu::ComboW->Values[ 0 ], Menu::ComboW->Values[ 1 ] );
			}
			break;
		case Orbwalker::Mode::Laneclear:
			ZeriWalker::Clear();
			break;
		case Orbwalker::Mode::Lasthit:
			ZeriWalker::Lasthit();
			break;
		case Orbwalker::Mode::Harass:
			{
				if ( !ZeriWalker::Lasthit() )
				{
					ZeriWalker::Combo();
					ComboW( true, Menu::HarassW->Values[ 0 ], Menu::HarassW->Values[ 1 ] );
				}
				break;
			}

		case Orbwalker::Mode::Flee: break;
		case Orbwalker::Mode::None: break;
		default: ;
		}
	}

	void OnExecuteCastFrame( AIBaseClient* Sender, Spell::SpellCastInfo* Spell )
	{
		if ( !Sender || !Sender->IsMe() )
			return;

		if ( Orbwalker::ActiveMode != Orbwalker::Mode::Combo )
			return;

		if ( Spell->Slot() == Spell::SpellSlot_Q )
		{
			ComboE( true );
		}
	}

	void OnDrawWorld()
	{
		if ( Menu::DrawWallJumps->Value )
		{
			for ( const auto& Spot : WallJumps )
			{
				const auto distance = HudManager::GetInstance()->CursorTargetLogic()->CursorPosition().Distance2D(Spot.Position);
				int Alpha = 255;
				if(Menu::DrawWallJumpsOnlyClose->Value)
				{
					if (HudManager::GetInstance()->CursorTargetLogic()->CursorPosition().Distance2D(Spot.Position) <= 2000)
					{
						const auto Scale = (1700.f - std::fmaxf(distance - 300.f, 0)) / 1700.f;
						Alpha = 255 * Scale;
					}
					else
					{
						Alpha = 0;
					}
				}

				Vector2f Out;
				if ( r3dProjectToScreen( Spot.Position, Out ) )
				{
					Graphics::AddCircle( Spot.Position, 30.f, 4, Graphics::RGBA( 255, 255, 255, Alpha ) );
					Graphics::AddArrow( Spot.Position.Extend( Spot.CastPosition, 30.f ), Spot.Position.Extend( Spot.CastPosition, 280.f ), 4, Graphics::RGBA( 255, 255, 255, Alpha ) );
				}
			}
		}

		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

		if ( Menu::DrawQ->Value )
		{
			Graphics::AddCircle( PlayerPosition, Q.Range, 2, Menu::QColor->Value );
		}

		if ( Menu::DrawW->Value )
		{
			Graphics::AddCircle( PlayerPosition, W.Range, 2, IM_COL32( 247, 225, 215, 255 ) );
		}

		if ( Menu::DrawE->Value )
		{
			Graphics::AddCircle( PlayerPosition, E.Range, 2, IM_COL32( 222, 219, 210, 255 ) );
		}

		if ( Menu::DrawR->Value )
		{
			Graphics::AddCircle( PlayerPosition, R.Range, 2, IM_COL32( 237, 175, 184, 255 ) );
		}
	}

	void DrawJumpLine( const Vector3f& Start, const Vector3f& End )
	{
		auto DrawEnd = End.Extend( Start, 35.f );
		DrawEnd.y = NavMesh::GetInstance()->GetHeightForPosition( DrawEnd );
		const Vector3f CircleCenter = Vector3f{ End.x, NavMesh::GetInstance()->GetHeightForPosition( End ), End.z };
		Graphics::AddLine( Start, DrawEnd, 4, IM_COL32( 208, 220, 209, 255 ) );
		Graphics::AddCircle( CircleCenter, 35.f, 4, IM_COL32( 127, 159, 129, 255 ) );
	}

	void OnDraw()
	{
		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
		const Vector3f CursorPosition = HudManager::GetInstance()->CursorTargetLogic()->CursorPosition();

		if ( BuffCache::HasBuff( ObjectManager::GetPlayer(), RActiveBuff ) && Menu::DrawChainLightning->Value )
		{
			for ( const auto& DesiredTarget : TS->GetOrderedTargets() )
			{
				if ( !TargetSelector::IsTargetValidNonSkillshot( DesiredTarget ) )
					continue;

				for ( const auto& Client : ManagerTemplates::AIBases() )
				{
					// Ignore units we can not Q
					//
					if ( !Client || !Client->IsActive() || Client->NetworkID() == DesiredTarget->NetworkID() || !Client->IsValidTarget( Q.Range, true, PlayerPosition ) )
						continue;

					Prediction::PredictionResult Result;
					if ( !PredictionSuccess( Prediction::GetPrediction(Client, Q, PlayerPosition, PlayerPosition, false, &Result) ) )
						continue;

					// Check the chain
					//
					AIBaseClient* CurrentTarget = Client;
					std::vector<Vector3f> Chain = { CurrentTarget->Position() };
					std::vector<std::uint32_t> ChainIds = { CurrentTarget->NetworkID() };
					bool FoundValidChain = false;
					for ( int i = 0; i < 4; i++ )
					{
						CurrentTarget = GetNextChainTarget( CurrentTarget->Position(), ChainIds );

						// Chain ends before it could reach our target
						//
						if ( !CurrentTarget )
							break;

						Chain.push_back( CurrentTarget->Position() );
						ChainIds.push_back( CurrentTarget->NetworkID() );

						// Chain leads to our target
						//
						if ( CurrentTarget->NetworkID() == DesiredTarget->NetworkID() )
						{
							if ( Chain.size() <= 2 )
								continue;

							Graphics::AddLine( PlayerPosition, Chain[ 0 ], 3, IM_COL32( 255, 255, 0, 255 ) );

							for ( int j = 1; j < Chain.size(); j++ )
							{
								Graphics::AddLine( Chain[ j - 1 ], Chain[ j ], 3, IM_COL32( 255, 255, 0, 255 ) );
							}

							FoundValidChain = true;
							break;
						}
					}

					if ( FoundValidChain )
						break;
				}
			}
		}

		if ( Menu::DrawWallEndPos->Value && E.IsReady() )
		{
			constexpr float MAX_DISTANCE_OVER_WALLS = 3000.f;
			bool InWall = false;
			for ( float TotalDistance = 0.f; TotalDistance < MAX_DISTANCE_OVER_WALLS; TotalDistance += NavMesh::GetInstance()->CellWidth() )
			{
				if ( TotalDistance > E.Range + 50.f && !InWall )
					break;

				const auto Position = PlayerPosition.Extend( CursorPosition, TotalDistance );
				if ( !InWall && NavMesh::IsWall( Position ) )
				{
					InWall = true;
				}

				if ( InWall && !NavMesh::IsWall( Position ) )
				{
					DrawJumpLine( PlayerPosition, Position );
					break;
				}
			}
		}
	}

	bool OnCastSpell( Spell::kSpellSlot SpellSlot, Vector3f Start, Vector3f End, AttackableUnit* Target )
	{
		if ( SpellSlot != Spell::SpellSlot_R || !Menu::BlockR->Value )
			return true;

		if ( EnemyHeroTargetsInRange( ObjectManager::GetPlayer(), R.RangeSquared ) < 1 )
			return false;

		return true;
	}

	//Only called for units we can kill with an auto
	//
	bool ShouldProcessKillableClient( AIBaseClient* Client, bool IsCharged )
	{
		// Always allow attacks if we are in laneclear
		//
		if ( Orbwalker::ActiveMode == Orbwalker::Mode::Laneclear && !IsCharged )
			return true;

		// If the minion is ignored (we q it for kill) dont allow an attack
		// TODO redundant as orb already checks it
		//
		if ( Orbwalker::ClearMode::CheckIgnored( Client ) )
			return false;

		// If we dont have Q allow always
		//
		if ( !Q.IsReady() )
			return true;

		Prediction::PredictionResult Result;
		const auto Status = Prediction::GetPrediction( Client, Q, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), false, &Result );

		//TODO maybe just return !PredictionSuccess( Status )

		// If its a lane minion only allow if we cant hit it
		//
		if ( Client->IsLaneMinion() )
		{
			return !PredictionSuccess( Status );
		}

		// Every other unit only if charged
		//
		return IsCharged;
	}

	void OnOrbwalkerAction( Features::Orbwalker::ActionArgs* Args, bool* Process )
	{
		if ( Args->Type != Orbwalker::ActionArgs::Before || !Args->Target )
			return;

		// Ignore target we target with Q
		//
		if ( ZeriWalker::IgnoreTargetId != 0 && Args->Target->NetworkID() == ZeriWalker::IgnoreTargetId )
		{
			*Process = false;
			return;
		}

		const auto IsCharged = BuffCache::HasBuff( ObjectManager::GetPlayer(), LaserReadyBuff );
		bool ShouldProcess = false;

		if ( Args->Intent != Orbwalker::ActionArgs::eIntent::Kill )
		{
			if ( const auto Client = Args->Target->AsAIBase() )
			{
				// Always Attack Wards, other units (jungle monsters, pets etc) only if charged
				//
				if ( Client->IsWard() )
				{
					ShouldProcess = true;
				}
				else if ( const auto Hero = Client->AsHero() )
				{
					auto CanQ = [] ( AIHeroClient* Target ) -> bool
					{
						if ( Q.CooldownTimeRemaining() > 0.1f )
							return false;

						Prediction::PredictionResult Result;
						const auto Status = Prediction::GetPrediction( Target, Q, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), false, &Result );

						return PredictionSuccess( Status );
					};

					// Always allow auto if can kill, otherwise only if we can't Q
					//
					if ( Hero->GetRealHealth( false, true ) < Damage::CalculateAutoAttackDamage( ObjectManager::GetPlayer(), Args->Target ) )
					{
						ShouldProcess = true;
					}
					else if ( IsCharged )
					{
						ShouldProcess = !CanQ( Hero );
					}
				}
				else
				{
					ShouldProcess = Client->IsLaneMinion() ? false : IsCharged;
				}
			}
			else
			{
				if ( IsCharged )
				{
					ShouldProcess = true;
				}
				else
				{
					ShouldProcess = Args->Target->GetHealth() < Damage::CalculateAutoAttackDamage( ObjectManager::GetPlayer(), Args->Target );
				}
			}
		}
		else
		{
			// Process killable targets
			//
			if ( const auto Client = Args->Target->AsAIBase() )
			{
				ShouldProcess = ShouldProcessKillableClient( Client, IsCharged );
			}
			else
			{
				// Buildings
				//
				if ( IsCharged )
				{
					ShouldProcess = true;
				}
				else
				{
					ShouldProcess = Args->Target->GetHealth() < Damage::CalculateAutoAttackDamage( ObjectManager::GetPlayer(), Args->Target );
				}
			}
		}

		*Process = ShouldProcess;
	}

	std::uint32_t MissileCount = 0;
	std::uint32_t EmpMissileCount = 0;

	void OnDelete( GameObject* Object )
	{
		if ( !Object->IsMissile() )
			return;

		const bool IsNormal = Object->Name() == xorstr_( "ZeriQMis" );
		const bool IsEmp = Object->Name() == xorstr_( "ZeriQMisEmpowered" );
		const std::uint32_t MaxCount = IsEmp ? 3 : 7;

		if ( IsNormal || IsEmp )
		{
			MissileCount++;
			if ( MissileCount >= MaxCount )
			{
				ZeriWalker::IgnoreTargetId = 0;
				MissileCount = 0;
			}
		}
	}

#pragma endregion Events

	void Initialize()
	{
		EventHandler::Add( Event::OnGameUpdate, OnGameUpdate );
		EventHandler::Add( Event::OnExecuteCastFrame, OnExecuteCastFrame );
		EventHandler::Add( Event::WorldDraw, OnDrawWorld );
		EventHandler::Add( Event::HudDraw, OnDraw );
		EventHandler::Add( Event::OnOrbwalkerAction, OnOrbwalkerAction );
		EventHandler::Add( Event::OnCastSpell, OnCastSpell );
		EventHandler::Add( Event::OnDeleteObject, OnDelete );

		Q = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_Q,
			.Type = Linear,
			.CastDelay = .0f,
			.Range = 750.f,
			.RangeType = Center,
			.Speed = 2600.f,
			.Radius = 40.f,
			.HeroCollisionType = Hard,
			.MinionCollisionType = Hard,
			.TowerCollisionType = Hard,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		W = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_W,
			.Type = Linear,
			.CastDelay = .10f,
			.Range = 1200.f,
			.RangeType = Center,
			.Speed = 2500.f,
			.Radius = 40.f,
			.HeroCollisionType = Hard,
			.MinionCollisionType = Hard,
			.TowerCollisionType = SpellCollisionType::None,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl,
			.ExtraCollisionRadius = 20.f
		} );

		WBeam = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_W,
			.Type = Linear,
			.CastDelay = 0.85f, // Handled somewhere else
			.Range = 1500.f,
			.RangeType = Edge,
			.Speed = FLT_MAX,
			.Radius = 100.f,
			.HeroCollisionType = Soft,
			.MinionCollisionType = Soft,
			.TowerCollisionType = SpellCollisionType::None
		} );

		E = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_E,
			.Range = 300.f,
		} );

		R = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_R,
			.CastDelay = .25f,
			.Range = 825.f
		} );

		const auto Main = MenuSDK::Menu::CreateMenu( xorstr_( "BlueZeri" ), xorstr_( "Blue Zeri" ) );
		Main->AddTexture( ObjectManager::GetPlayer()->GetIconSquare() );

		const auto MenuP = Main->AddMenu( xorstr_( "MenuP" ), xorstr_( "[P / Living Battery] Settings" ) );
		MenuP->AddTexture( ObjectManager::GetPlayer()->GetPassiveIcon() );
		{
			Menu::ComboAA = MenuP->AddMultiList( xorstr_( "ComboAA" ), xorstr_( "Combo Usage" ), { xorstr_( "Laser Ready" ), xorstr_( "Always" ) }, { 0 } );
			Menu::HarassAA = MenuP->AddMultiList( xorstr_( "HarassAA" ), xorstr_( "Harass Usage" ), { xorstr_( "Laser Ready" ), xorstr_( "Always" ) }, { 0 } );
			Menu::LaneAA = MenuP->AddList( xorstr_( "LaneAA" ), xorstr_( "Laneclear Usage" ), { xorstr_( "Execute" ), xorstr_( "Always" ) }, 0 );
			Menu::JungleAA = MenuP->AddList( xorstr_( "JungleAA" ), xorstr_( "Jungleclear Usage" ), { xorstr_( "Execute" ), xorstr_( "Always" ) }, 0 );
			Menu::LasthitAA = MenuP->AddCheckBox( xorstr_( "LastAA" ), xorstr_( "Execute Minions with AA" ), true );
		}

		MenuP->Hide( true );

		const auto MenuQ = Main->AddMenu( xorstr_( "MenuQ" ), xorstr_( "[Q / Burst Fire] Settings" ) );
		MenuQ->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_Q )->GetIcon() );
		{
			Menu::ComboQ = MenuQ->AddCheckBox( xorstr_( "ComboQ" ), xorstr_( "Use in Combo" ), true );
			Menu::ComboQIgnoreCollision = MenuQ->AddCheckBox( xorstr_( "ComboQCol" ), xorstr_( "Ignore Collision" ), false );
			Menu::ChainLogic = MenuQ->AddCheckBox( xorstr_( "ComboQChain" ), xorstr_( "Overcharged Logic" ), false );
			Menu::ChainLogicOnlyKeepAlive = MenuQ->AddCheckBox( xorstr_( "ComboQChainKeepAlive" ), xorstr_( "^ Only to keep R Up" ), false );
			Menu::HarassQ = MenuQ->AddCheckBox( xorstr_( "HarassQ" ), xorstr_( "Use in Harass" ), true );

			Menu::LasthitQ = MenuQ->AddCheckBox( xorstr_( "LastQ" ), xorstr_( "Use in Last Hit" ), true );
			Menu::LaneQ = MenuQ->AddCheckBox( xorstr_( "LaneQ" ), xorstr_( "Use in Lane Clear" ), true );
			Menu::JungleQ = MenuQ->AddCheckBox( xorstr_( "JungleQ" ), xorstr_( "Use in Jungle Clear" ), true );

			MenuQ->AddInfo( xorstr_( "SpacerLC" ), xorstr_( "Laneclear Settings" ) );

			Menu::LaneQPlant = MenuQ->AddCheckBox( xorstr_( "LaneQPlant" ), xorstr_( "Target Plants" ), true );

			MenuQ->AddInfo( xorstr_( "Spacer" ), xorstr_( "" ) );

			Menu::DrawQ = MenuQ->AddCheckBox( xorstr_( "DrawQ" ), xorstr_( "Draw Range" ), true );
			Menu::QColor = MenuQ->AddColorPicker( xorstr_( "DrawQCol" ), xorstr_( "Color" ), IM_COL32( 176, 196, 177, 255 ) );
		}

		const auto MenuW = Main->AddMenu( xorstr_( "MenuW" ), xorstr_( "[W / Ultrashock Laser] Settings" ) );
		MenuW->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_W )->GetIcon() );
		{
			Menu::ComboW = MenuW->AddMultiList( xorstr_( "ComboW" ), xorstr_( "Use in Combo" ), { xorstr_( "Missile" ), xorstr_( "Beam" ) }, { 0, 1 } );
			Menu::HarassW = MenuW->AddMultiList( xorstr_( "HarassW" ), xorstr_( "Use in Harass" ), { xorstr_( "Missile" ), xorstr_( "Beam" ) }, { 0, 1 } );
			Menu::OnlyOutOfRangeW = MenuW->AddCheckBox( xorstr_( "OutOfRangeW" ), xorstr_( "Block If In Q Range" ), true );
			Menu::KillstealW = MenuW->AddCheckBox( xorstr_( "KSW" ), xorstr_( "Killsteal" ), true );
			Menu::SemiAutoW = MenuW->AddKeyBind( xorstr_( "SemiAutoW" ), xorstr_( "Semi-Auto" ), 'T', false, false );

			MenuSDK::PermaShow::AddComponent( xorstr_( "Semi-Auto [W]" ), Menu::SemiAutoW );

			MenuW->AddInfo( xorstr_( "Spacer" ), xorstr_( "" ) );

			Menu::HitchanceW = MenuW->AddList( xorstr_( "HitchanceW" ), xorstr_( "Missile Hitchance" ), { xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High (Slow)" ), xorstr_( "Very High (Very Slow)" ), xorstr_( "Only Dashes" ) }, 2 );
			Menu::HitchanceBeam = MenuW->AddList( xorstr_( "HitchanceBeam" ), xorstr_( "Beam Hitchance" ), { xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High (Slow)" ), xorstr_( "Very High (Very Slow)" ), xorstr_( "Only Dashes" ) }, 3 );

			MenuW->AddInfo( xorstr_( "Spacer2" ), xorstr_( "" ) );

			Menu::DrawW = MenuW->AddCheckBox( xorstr_( "DrawW" ), xorstr_( "Draw Missile Range" ), true );
		}

		const auto MenuE = Main->AddMenu( xorstr_( "MenuE" ), xorstr_( "[E / Spark Surge] Settings" ) );
		MenuE->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_E )->GetIcon() );
		{
			Menu::ComboE = MenuE->AddMultiList( xorstr_( "ComboE" ), xorstr_( "Use in Combo" ), { "Killable", "Collision" }, { 0, 1 } );
			Menu::OnlyReset = MenuE->AddCheckBox( xorstr_( "OnlyResetE" ), xorstr_( "Only Q>E>Q" ), false );
			Menu::CollisionIgnoreReset = MenuE->AddCheckBox( xorstr_( "CollisionIgnoreReset" ), xorstr_( "^ Unless Collision" ), true );
			Menu::EMode = MenuE->AddList( xorstr_( "EMode" ), xorstr_( "E Mode" ), { xorstr_( "Cursor" ), xorstr_( "Safe" ) }, 1 );
			Menu::EnemyLimitE = MenuE->AddSlider( xorstr_( "EnemyLimitE" ), xorstr_( "Max Enemy Count" ), 1, 1, 5, 1 );
			Menu::JumpE = MenuE->AddKeyBind( xorstr_( "JumpKey" ), xorstr_( "Wall Jump Key" ), 'G', false, false );
			Menu::SaveManaE = MenuE->AddCheckBox( xorstr_( "SaveManaE" ), xorstr_( "Save Mana for E" ), false );

			MenuSDK::PermaShow::AddComponent( xorstr_( "Wall Jump" ), Menu::JumpE );

			MenuE->AddInfo( xorstr_( "Spacer" ), xorstr_( "" ) );

			Menu::DrawE = MenuE->AddCheckBox( xorstr_( "DrawE" ), xorstr_( "Draw Range" ), true );
			Menu::DrawWallJumps = MenuE->AddCheckBox( xorstr_( "DrawWallJumps" ), xorstr_( "Draw Wall Jumps" ), true );
			Menu::DrawWallJumps = MenuE->AddCheckBox( xorstr_( "DrawWallJumps" ), xorstr_( "Draw Wall Jumps" ), true );
			Menu::DrawWallJumpsOnlyClose = MenuE->AddCheckBox( xorstr_( "DrawWallJumpsOnlyClose" ), xorstr_( "^ Only if near cursor" ), true );
			Menu::DrawWallEndPos = MenuE->AddCheckBox( xorstr_( "DrawWallEndPos" ), xorstr_( "Draw Jump Line" ), true );
		}

		const auto MenuR = Main->AddMenu( xorstr_( "MenuR" ), xorstr_( "[R / Lightning Crash] Settings" ) );
		MenuR->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_R )->GetIcon() );
		{
			Menu::ComboR = MenuR->AddMultiList( xorstr_( "ComboRModes" ), xorstr_( "Combo Usage" ), { xorstr_( "Killable" ), xorstr_( "Count" ) }, { 0, 1 } );
			Menu::RCount = MenuR->AddSlider( xorstr_( "RCount" ), xorstr_( "Min Enemies" ), 2, 1, 5 );
			Menu::BlockR = MenuR->AddCheckBox( xorstr_( "BlockR" ), xorstr_( "Block If No Targets" ), true );
			Menu::DrawR = MenuR->AddCheckBox( xorstr_( "DrawR" ), xorstr_( "Draw Range" ), true );
			Menu::DrawChainLightning = MenuR->AddCheckBox( xorstr_( "DrawChainLightning" ), xorstr_( "Draw Best Arc" ), true );
		}

		Utility::WatchedBuffs[ EBuff ] = { xorstr_( "[E] Buff" ), true };
		Utility::WatchedBuffs[ RActiveBuff ] = { xorstr_( "[R] Active" ), true };
		Utility::WatchedBuffs[ RSpeedBuff ] = { xorstr_( "[R] MS" ), true };
		Utility::WatchedBuffs[ GhostBuff ] = { xorstr_( "[Ghost]" ), true };

		auto DamageFunction = [&] ( AIBaseClient* Target )
		{
			const float Damage = GetQDamage( Target );

			return ( Damage / Target->GetRealHealth( true ) ) * 100.f;
		};

		TS = TargetSelector::GetInstance( DamageFunction );

		InitializeWallJumps();
	}
}
